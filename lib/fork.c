// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	// uvpt 是 JOS 提供的用户页表魔术指针，可以直接通过虚拟地址查到对应的 PTE
	if ((err & FEC_WR) == 0 || (uvpt[PGNUM(addr)] & PTE_COW) == 0) {
		panic("pgfault: not a copy-on-write fault! va: %x, err: %x", addr, err);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	// 向内核申请一页全新的物理内存，映射到 PFTEMP 这个临时地址，权限为可写
	if ((r = sys_page_alloc(0, (void *)PFTEMP, PTE_P | PTE_U | PTE_W)) < 0) {
		panic("pgfault: sys_page_alloc failed %e", r);
	}
	// 将原本那页的数据复制到新分配的页里，注意这里要把 addr 向下取整到页边界
	addr = ROUNDDOWN(addr, PGSIZE);
	memmove(PFTEMP, addr, PGSIZE);

	// 将新分配的这页内存，映射回那个发生缺页的地址，权限恢复为可写
	if ((r = sys_page_map(0, (void *)PFTEMP, 0, addr, PTE_P | PTE_U | PTE_W)) < 0) {
		panic("pgfault: sys_page_map failed %e", r);
	}

	// 记得解除临时地址 PFTEMP 的映射
	if ((r = sys_page_unmap(0, (void *)PFTEMP)) < 0) {
		panic("pgfault: sys_page_unmap failed %e", r);
	}
	// panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	void *addr = (void *)(pn * PGSIZE);

	// LAB 4: Your code here.
	pte_t pte = uvpt[pn];

	// 如果页表项带有 PTE_SHARE 标志，直接原样映射，不需要设置 OW
	if (pte & PTE_SHARE) {
		r = sys_page_map(0, addr, envid, addr, pte & PTE_SYSCALL);
		if (r < 0) {
			panic("duppage: sys_page_map PTE_SHARE failed: %e", r);
		}
		return 0;
	}
	
	// 如果页是可写的，或者是写时复制的，需要共享为 COW
	if ((pte & PTE_W) || (pte & PTE_COW)) {
		// 先映射给子进程，标记为 COW 和只读
		if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U | PTE_COW)) < 0) {
			panic("duppage: sys_page_map child failed %e", r);
		}
		// 然后再重映射自己，剥夺自己的写权限，也标记为 COW
		if ((r = sys_page_map(0, addr, 0, addr, PTE_P | PTE_U | PTE_COW)) < 0) {
			panic("duppage: sys_page_map parent failed %e", r);
		}
	}
	else {
		// 如果页本身就只是只读的，那直接原样映射给子进程，不需要 COW
		if ((r = sys_page_map(0, addr, envid, addr, PTE_P | PTE_U)) < 0) {
			panic("duppage: sys_page_map read-only failed %e", r);
		}
	}
	// panic("duppage not implemented");
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	envid_t envid;
	uint32_t addr;
	int r;

	set_pgfault_handler(pgfault);

	// 创建空壳子进程
	envid = sys_exofork();
	if (envid < 0) {
		panic("sys_exofork failed: %e", envid);
	}
	if (envid == 0) {
		// 这里是子进程的代码路径，父进程把环境全布置好
		// 要修改的是重新设定 thisenv，因为它是全局变量，复制过来指向了父进程
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// 这里是父进程的代码路径，遍历整个用户空间地址 [0, UTOP)
	for (addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		// 先查页目录项是否存在，不存在的 4MB 空间直接跳过
		if ((uvpd[PDX(addr)] & PTE_P) == 0) {
			continue;
		}
		// 再查具体的页表项是否存在，且必须是用户可访问的
		if ((uvpt[PGNUM(addr)] & PTE_P) != 0 && (uvpt[PGNUM(addr)] & PTE_U) != 0) {
			duppage(envid, PGNUM(addr));
		}
	}

	// 为子进程分配专属的 Exception Stack 
	// 异常栈不能 COW，如果它 COW 了，子进程缺页时往异常栈压 Trapframe 时又会引发缺页，无限嵌套
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0) {
		panic("fork: sys_page_alloc exception stack failed %e", r);
	}

	// 替子进程向内核登记缺页异常汇编入口
	extern void _pgfault_upcall(void);
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0) {
		panic("fork: sys_env_set_pgfault_upcall failed %e", r);
	}

	// 唤醒子进程
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) {
		panic("fork: sys_env_set_status failed %e", r);
	}

	return envid;
	// panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
