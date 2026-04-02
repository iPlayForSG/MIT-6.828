从 blogs 分支摘录过来的，图片懒得替换了

### Challenge 1

> 我们消耗了许多物理页来保存 KERNBASE 映射的页表。使用页目录条目中的 `PTE_PS` ("Page Size") 位做一个更节省空间的工作。这个位在原始的 80386 中不受支持，但在较新的 x86 处理器上受支持。因此，你必须参考当前 Intel 手册的第 3 卷。确保你的内核设计仅在支持该优化的处理器上使用它！

为了把 256 MB 的物理内存映射到 KERNBASE，我们需要 256 MB / 4 KB = 65536 个 PTE。一个页表可以装 1024 个 PTE，所以我们需要消耗 64 个物理页（256 KB）。

既然这 256 MB 内存是连续映射的，那么我们可以直接用 4 MB 的页去搞定，并且只需要修改页目录表的 64 个 PDE，甚至不需要额外分配页表了。

现在我们就该替换一下之前映射 KERNBASE 的代码了

```c
// CHALLENGE 1:

// uint32_t mem_size = 0xFFFFFFFF - KERNBASE + 1;
// boot_map_region(kern_pgdir, KERNBASE, mem_size, 0, PTE_W | PTE_P);

uint32_t eax, ebx, ecx, edx;
cpuid(1, &eax, &ebx, &ecx, &edx);
if (edx & (1 << 3)) { // edx 的第三位是 PSE 
    cprintf("CPU Supports PSE.\n");
    uint32_t cr4 = rcr4() | CR4_PSE;
    lcr4(cr4);

    size_t i;
    for (i = 0; i < 64; ++i) {
        physaddr_t pa = i * PTSIZE;
        kern_pgdir[PDX(KERNBASE) + i] = pa | PTE_P | PTE_W | PTE_PS;
    }
}
else {
    cprintf("CPU Does Not Support PSE.\n");
}
```

然后

![[MIT 6.828] Lab 2 6](img\[MIT 6.828] Lab 2 6.png)

居然 kernel panic 了，然后发现其实是 check_va2pa 校验函数不能识别大页，直接对大页解引用，当然会导致 assertion 错误了。稍微修改一下 check_va2pa，新增一个大页校验。如果这是一个 4 MB 大页，那就该物理地址 = PDE 的高 10 位（大页基址） + 虚拟地址的低 22 位（页内偏移）。

```c
static physaddr_t
check_va2pa(pde_t *pgdir, uintptr_t va)
{
	pte_t *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;

	if (*pgdir & PTE_PS) {
        return PTE_ADDR(*pgdir) | (va & 0x003FFFFF);
    }

	p = (pte_t*) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}
```

现在再 make qemu 就没问题了。

### Challenge 2

>  扩展 JOS 内核监视器，增加以下命令：
>
>  - 以有用且易于阅读的格式显示适用于当前活动地址空间中特定范围的虚拟/线性地址的所有物理页映射（或缺乏映射）。例如，你可以输入 `'showmappings 0x3000 0x5000'` 来显示适用于虚拟地址 0x3000、0x4000 和 0x5000 的页面的物理页映射和相应的权限位。
>  - 显式设置、清除或更改当前地址空间中任何映射的权限。
>  - 给定虚拟或物理地址范围，转储内存范围的内容。确保存储转储代码在范围跨越页边界时表现正确！
>  - 做任何其他你认为以后可能对调试内核有用的事情。（很有可能会有用！）

开始修改 kern/monitor.c 吧

首先是 showmappings，其核心是遍历一段虚拟地址，调用 pgdir_walk获取物理地址和权限。

```c
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
```

然后是 setm，找到该页的 PTE，然后 |= 置 1，&= ~ 置 0 即可。一定要记住，修改页表后必须刷新 TLB，使原有的 CPU 缓存失效

```c
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
```

然后是 dumpm，注意专门强调了一下"确保存储转储代码在范围跨越页边界时表现正确！"那么在打印某个字节之前，首先检查它所在的页是否存在。

```c
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
```

然后 make qemu，来测试一下

首先是 showmappings

![[MIT 6.828] Lab 2 7](img\[MIT 6.828] Lab 2 7.png)

内核栈是可读可写用户不可见的，没什么问题

然后 dumpm，这里主要检查一下边界保护有没有生效

![[MIT 6.828] Lab 2 8](img\[MIT 6.828] Lab 2 8.png)

最后 setm，改完再用 showmapping 检查一下

![[MIT 6.828] Lab 2 9](img\[MIT 6.828] Lab 2 9.png)

确实被更改了，没什么问题。

### Challenge 3

> 每个用户级环境都映射了内核。更改 JOS，使内核拥有自己的页表，并使一用户级环境在运行时只映射最少数量的内核页。也就是说，每个用户级环境只映射刚好足够让用户级环境正确进入和离开内核的页。你还必须想出一个计划，让内核读取/写入系统调用的参数。

如果要写的话感觉有点变态，所以说一下思路

首先，“用户级环境在运行时只映射最少数量的内核页”。这个过程中，CPU 先收到中断信号，读 IDT；然后发现需要切 r0，然后就去读任务状态段去找内核栈地址；CPU 将用户的寄存器压入内核栈；CPU 跳转到中断入口，开始执行内核指令。

所以，用户页表内至少得映射四个内核页面：存 IDT、GDT 的页（R）；存 TSS 的页（R）；存内核栈的页（RW）；存中断入口的页（RX）。其它的就不需要了

然后如何维护？维护两套页表，一套用户态，一套内核态。用户态的里面塞用户数据、代码，以及上面这四个页；内核态里面就是完整内核映射。

最后，“让内核读取/写入系统调用的参数”。按我们的设想，一进入中断，内核就切换到了内核页表，如果没映射用户的低虚拟地址，那么内核一读指针就 Page Fault 了。

怎么解决呢？我们可以设一个缓冲区，分配一个 1 页大小的共享内存，这个页同时存在于用户态和内核态中。当用户想传数据给内核态时，就把数据拷贝在这个共享缓冲区中。内核直接在缓冲区内读取数据即可。


### Challenge 4

> 写一个大纲，说明如何设计一个内核以允许用户环境无限制地使用完整的 4GB 虚拟和线性地址空间。提示：先做前一个挑战练习，将内核减少到用户环境中的几个映射。提示：该技术有时被称为“跟随跳动的内核 (follow the bouncing kernel)”。在你的设计中，一定要确切地说明当处理器在内核模式和用户模式之间转换时必须发生什么，以及内核将如何完成这种转换。还要描述在这种方案中内核将如何访问物理内存和 I/O 设备，以及内核在系统调用等期间将如何访问用户环境的虚拟地址空间。最后，思考并描述这种方案在灵活性、性能、内核复杂性和你能想到的其他因素方面的优缺点。

这个更变态。

在 x86 架构中，当发生系统调用时，CPU 必须立刻从 IDT 找到中断处理程序的线性地址并执行。也就是说，内核的中断入口必须时刻存在于当前的线性地址空间中。如果想让用户环境无限制地使用整个空间，那只能让内核的中断入口不固定位置，随时换位置。

那么，首先内核得随时监控用户进程的内存分配情况。当用户态有空闲的、未被映射的页，内核就把自己的入口代码映射到那里。此时内核还得更新寄存器（IDTR、GDTR、TR 等），告诉 CPU 内核状态更新了。并且，如果用户态此时 malloc 申请了内核目前所在的线性地址，内核得立刻寻找下一个空闲地址，把入口代码移动“跳动”过去。

如何进行状态转换呢？

用户态进入内核态时，发生系统调用，硬件根据寄存器地址直接跳入当前入口。此时依然用的是用户态的页表，只有入口页是内核态的。入口处的汇编将寄存器压入入口中的临时栈，`mov eax, PADDR(kern_pgdir); mov cr3, eax`，切换完成。

内核态进入用户态时，首先跳转到出口代码处，此时这个代码应该位于内核页表和用户页表中同一物理地址。然后就是`mov eax, PADDR(user_pgdir); mov cr3, eax`，`iret`，返回用户态。

物理内存和 I/O 设备的访问相对来说就简单一些。按照我们的设计，内核态会拥有整个 4GB 空间，内核可以直接从虚拟地址 0x00000000 开始，把所有的 RAM 直接一对一映射到自己的页表中，I/O 设备也是如此。

当内核接管 CPU 时，CR3 已经切换成了内核页表。此时如果用户态传入了一个指针，内核直接读这个虚拟地址，读到的是内核空间的数据，就 Page Fault 了。这就涉及到了系统调用的参数传递的问题。

内核虽然看不到用户的虚拟地址，但它映射了所有的物理内存。所以可以直接拿到用户的虚拟指针 VA，然后找到当前用户的页目录表物理基址。然后，用类似 Exercise 4 `pgdir_walk()` 的方式，模拟 CPU的查表过程，在用户的页目录和页表中，查出 VA 对应的真实物理地址 PA。最终，内核通过自己的物理内存映射区，直接读取物理地址 PA 里的数据。

最后是优缺点

优点很明显，它的空间利用率很好，用户态直接获得了 4GB 的空间，而且页表隔离的安全性也很好，毕竟用户页表里面除了那个入口代码以外根本没有内核的映射。

缺点也是显著的，它的性能开销太大了，每次系统调用、中断，都得切 CR3。而且明显能感觉到入口代码的维护难度特别大，不仅如此，估计这里读数据的开销也很大。

### Challenge 5

>  由于我们的 JOS 内核的内存管理系统仅以页粒度分配和释放内存，我们没有类似于通用的 `malloc/free` 设施可以在内核中使用。如果我们想要支持某些需要大于 4KB 的**物理连续**缓冲区的 I/O 设备，或者如果我们希望用户级环境（而不仅仅是内核）能够分配和映射 4MB **大页 (superpages)** 以获得最大的处理器效率，这可能是一个问题。（参见前面关于 PTE_PS 的挑战问题。）

这个问题现代 Linux 的“伙伴系统”（Buddy System）可以解决。大概就是，它会把内存按 2 的幂次方去划分，并两两配对。这里引入一个概念叫阶（Order）

* Order 0 = $2^0 \times 4\text{KB} = 4\text{KB}$ （单页）

* Order 1 = $2^1 \times 4\text{KB} = 8\text{KB}$ （两个连续页）

* ...

* Order 10 = $2^{10} \times 4\text{KB} = 4\text{MB}$ （1024 个连续页，刚好一个大页）

假设系统现在只有一块完整的 4MB（Order 10）空闲内存。用户申请一块 8KB（Order 1）的内存。此时内存分配器发现没有现成的 Order 1，于是把 4MB 的块一分为二，变成两个 2MB（Order 9）的“伙伴”，然后继续把其中一个 2MB 一分为二，变成两个 1MB（Order 8）……一路划分，直到分出两个 8KB（Order 1）的块。随后，把其中一个 8KB 给用户，另一个 8KB 挂到 Order 1 的空闲链表上。

当用户归还那块 8KB 内存时，内存分配器首先检查这个内存的”伙伴“是否空闲，如果空闲，就把它俩合并，拼成一个 16KB（Order 2）的块。然后递归，如果一路空闲，最终会拼回 4MB（Order 10），如果有块不空闲就停下。

这样的话，之前的`page_free_list`就得从链表改成数组，每个元素代表一个 Order 的空闲链表。`struct PageInfo`也得修改，让每个页能记住自己当前所属的 Order，以及它是否空闲。

如何寻找伙伴？如果当前块的首页索引是 page_idx，它的块大小是 1 << order（页数），那么那么它的伙伴的索引 `buddy_idx = page_idx ^ (1 << order);`，跟树有点像。

听起来好像很简单，但是我没在 JOS 里面写出来，以后有机会补一下，至少我感觉我的思路没啥问题。

