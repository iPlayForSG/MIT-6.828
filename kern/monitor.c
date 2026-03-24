// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.
// CHALLENGE 2:
#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmappings", "Display physical page mappings and permissions (showmappings 0x<begin_va> 0x<end_va>)", mon_showmappings },
    { "setm", "Set or clear permission bits (setm <va> <P|W|U> <0|1>)", mon_setm },
    { "dumpm", "Dump memory contents (dumpm <-v|-p> <addr> <nwords>)", mon_dumpm },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.

	// Lab 1 Exercise 11
	// uint32_t ebp = read_ebp();
	// uint32_t *p = (uint32_t *)ebp;
	// cprintf("Stack backtrace:\n");

	// while (ebp != 0) {
	// 	p = (uint32_t *) ebp;
	// 	cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, *(p + 1), *(p + 2), *(p + 3), *(p + 4), *(p + 5), *(p + 6));
	// 	ebp = *p;
	// }

	// Lab 1 Exercise 12
	uint32_t ebp = read_ebp();
	uint32_t *p = (uint32_t *)ebp;
	cprintf("Stack backtrace:\n");

    while (ebp != 0) {
        p = (uint32_t *) ebp;
        cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, *(p + 1), *(p + 2), *(p + 3), *(p + 4), *(p + 5), *(p + 6));

		uint32_t eip = *(p + 1);
        struct Eipdebuginfo info;
        if (debuginfo_eip(eip, &info) == 0) {
            cprintf("       %s:%d: %.*s+%u\n", info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
        }
        ebp = *p;
    }
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 3) {
        cprintf("Usage: showmappings 0x<begin_va> 0x<end_va>\n");
        return 0;
    }

    // 转换为十六进制地址
    uintptr_t start = strtol(argv[1], NULL, 16);
    uintptr_t end = strtol(argv[2], NULL, 16);

    // 按页对齐
    start = ROUNDDOWN(start, PGSIZE);
    end = ROUNDUP(end, PGSIZE);

    cprintf("    VA       |     PA       | P W U\n");
    cprintf("-----------------------------------\n");

    for (; start <= end; start += PGSIZE) {
        pte_t *pte = pgdir_walk(kern_pgdir, (void *)start, 0);
        if (!pte || !(*pte & PTE_P)) {
            cprintf(" 0x%08x  |  <unmapped>  | - - -\n", start);
        }
		else {
            int p = (*pte & PTE_P) ? 1 : 0;
            int w = (*pte & PTE_W) ? 1 : 0;
            int u = (*pte & PTE_U) ? 1 : 0;
            cprintf(" 0x%08x  |  0x%08x  | %d %d %d\n", start, PTE_ADDR(*pte), p, w, u);
        }
        if (start >= 0xFFFFF000) break; // 防止地址溢出，导致死循环
    }
    return 0;
}

int
mon_setm(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 4) {
        cprintf("Usage: setm 0x<va> <P|W|U> <0|1>\n");
        return 0;
    }

    uintptr_t va = strtol(argv[1], NULL, 16);
    char flag = argv[2][0];
    int val = strtol(argv[3], NULL, 10);

    pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);
    if (!pte || !(*pte & PTE_P)) {
        cprintf("Error: Virtual address 0x%08x is not mapped.\n", va);
        return 0;
    }

    uint32_t mask = 0;
    if (flag == 'P' || flag == 'p') mask = PTE_P;
    else if (flag == 'W' || flag == 'w') mask = PTE_W;
    else if (flag == 'U' || flag == 'u') mask = PTE_U;
    else {
        cprintf("Error: Invalid flag. Use P, W, or U.\n");
        return 0;
    }

    if (val) {
        *pte |= mask;
    }
	else {
        *pte &= ~mask;
    }

    // 修改页表后必须使原有的 CPU 缓存失效
    tlb_invalidate(kern_pgdir, (void *)va);
    cprintf("Successfully updated permission at 0x%08x.\n", va);
    return 0;
}

int
mon_dumpm(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 4) {
        cprintf("Usage: dumpm <-v|-p> 0x<addr> <num_words>\n");
        return 0;
    }

    int is_virtual = (argv[1][1] == 'v');
    uintptr_t addr = strtol(argv[2], NULL, 16);
    uint32_t count = strtol(argv[3], NULL, 10);

    cprintf("Dumping %d words starting at %s 0x%08x:\n", count, is_virtual ? "VA" : "PA", addr);

    uint32_t i;
    for (i = 0; i < count; i++) {
        uintptr_t current_addr = addr + i * 4;
        uintptr_t read_va;

        if (is_virtual) {
            read_va = current_addr;
            // 检查当前地址所在的页是否映射
            pte_t *pte = pgdir_walk(kern_pgdir, (void *)ROUNDDOWN(read_va, PGSIZE), 0);
            if (!pte || !(*pte & PTE_P)) {
                cprintf("\nError: Page boundary crossed into unmapped VA: 0x%08x. Stopping.\n", read_va);
                break;
            }
        } else {
            // 物理地址转内核虚拟地址, 必须小于机器总物理内存
            if (current_addr >= npages * PGSIZE) {
                cprintf("\nError: Physical address 0x%08x out of bounds. Stopping.\n", current_addr);
                break;
            }
            read_va = (uintptr_t)KADDR(current_addr);
        }

        if (i % 4 == 0) cprintf("\n0x%08x: ", current_addr);
        
        cprintf("%08x ", *(uint32_t *)read_va);
    }
    cprintf("\n");
    return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
