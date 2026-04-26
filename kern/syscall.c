/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, s, len, PTE_U);
	
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *child_env;
	int r;

	// 分配新环境，父进程是 curenv
	if ((r = env_alloc(&child_env, curenv->env_id)) < 0) {
		return r;
	}

	child_env->env_status = ENV_NOT_RUNNABLE;
	child_env->env_tf = curenv->env_tf;
	// 修改子进程的 eax 为 0，这样子进程从 sys_exofork 返回时拿到的是 0
	child_env->env_tf.tf_regs.reg_eax = 0;

	return child_env->env_id;
	// panic("sys_exofork not implemented");
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env *e;
	int r;
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) {
		return -E_INVAL;
	}
	// 查找目标环境，checkperm = 1 代表检查权限
	if ((r = envid2env(envid, &e, 1)) < 0) {
		return r;
	}
	e->env_status = status;
	return 0;
	// panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	// panic("sys_env_set_trapframe not implemented");
	struct Env *e;
	int r;

	// 找到对应的环境，权限检查：只能是自己或子进程
	r = envid2env(envid, &e, 1);
	if (r < 0) {
		return r;
	}

	// 将用户传过来的 Trapframe 复制到目标环境的 env_tf 里
	e->env_tf = *tf;
	// 不能让用户进程给自己提权，设置代码段的 RPL 为 3
	e->env_tf.tf_cs |= 3;
	// 开启中断标志位，保证进程能被时钟中断打断
	e->env_tf.tf_eflags |= FL_IF;
	// 把 IOPL 权限抹掉
	e->env_tf.tf_eflags &= ~FL_IOPL_MASK;

	return 0;


}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *e;
	int r;
	if ((r = envid2env(envid, &e, 1)) < 0) {
		return r;
	}
	e->env_pgfault_upcall = func;
	return 0;

	// panic("sys_env_set_pgfault_upcall not implemented");
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	struct Env *e;
	struct PageInfo *pp;
	int r;
	// 地址合法性与对齐
	if ((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0) {
		return -E_INVAL;
	}
	// 权限标志位检查，必须有 U 和 P，不能有保留位
	if ((perm & PTE_U) == 0 || (perm & PTE_P) == 0 || (perm & ~PTE_SYSCALL) != 0) {
		return -E_INVAL;
	}
	if ((r = envid2env(envid, &e, 1)) < 0) {
		return r;
	}
	if ((pp = page_alloc(ALLOC_ZERO)) == NULL) {
		return -E_NO_MEM;
	}
	if ((r = page_insert(e->env_pgdir, pp, va, perm)) < 0) {
		page_free(pp);
		return r;
	}
	return 0;
	// panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	struct Env *srcenv, *dstenv;
	struct PageInfo *pp;
	pte_t *pte;
	int r;

	// 地址与对齐检查
	if ((uint32_t)srcva >= UTOP || (uint32_t)srcva % PGSIZE != 0 ||
	    (uint32_t)dstva >= UTOP || (uint32_t)dstva % PGSIZE != 0) {
		return -E_INVAL;
	}
	// 权限检查
	if ((perm & PTE_U) == 0 || (perm & PTE_P) == 0 || (perm & ~PTE_SYSCALL) != 0) {
		return -E_INVAL;
	}

	if ((r = envid2env(srcenvid, &srcenv, 1)) < 0 ||
	    (r = envid2env(dstenvid, &dstenv, 1)) < 0) {
		return r;
	}

	// 查找源地址映射的物理页
	if ((pp = page_lookup(srcenv->env_pgdir, srcva, &pte)) == NULL) {
		return -E_INVAL;
	}

	// 如果想以写权限映射，源页表必须也可写
	if ((perm & PTE_W) != 0 && (*pte & PTE_W) == 0) {
		return -E_INVAL;
	}

	// 建立映射
	if ((r = page_insert(dstenv->env_pgdir, pp, dstva, perm)) < 0) {
		return r;
	}

	return 0;
	// panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	struct Env *e;
	int r;

	if ((uint32_t)va >= UTOP || (uint32_t)va % PGSIZE != 0) {
		return -E_INVAL;
	}

	if ((r = envid2env(envid, &e, 1)) < 0) {
		return r;
	}

	page_remove(e->env_pgdir, va);
	return 0;
	// panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *target;
	int r;

	// 查找目标进程。注意 checkperm = 0，因为任何进程都可以给任何进程发消息
	if ((r = envid2env(envid, &target, 0)) < 0) {
		return r;
	}

	// 检查目标是否在等待接收
	if (!target->env_ipc_recving) {
		return -E_IPC_NOT_RECV;
	}

	// 处理可选的物理页共享逻辑
	if ((uint32_t)srcva < UTOP) {

		// 发送方想发页，检查地址边界与对齐
		if ((uint32_t)srcva % PGSIZE != 0) return -E_INVAL;

		// 检查权限位是否合法，必须有 U 和 P
		if ((perm & PTE_U) == 0 || (perm & PTE_P) == 0 || (perm & ~PTE_SYSCALL) != 0) return -E_INVAL;

		pte_t *pte;
		struct PageInfo *pp = page_lookup(curenv->env_pgdir, srcva, &pte);

		// 发送方该地址根本没映射物理页
		if (!pp) return -E_INVAL; 

		// 如果试图以写权限发送，发送方自己必须也有写权限
		if ((perm & PTE_W) && (*pte & PTE_W) == 0) return -E_INVAL;

		// 如果目标也愿意接收页 (dstva < UTOP)
		if ((uint32_t)target->env_ipc_dstva < UTOP) {
			if ((r = page_insert(target->env_pgdir, pp, target->env_ipc_dstva, perm)) < 0) {
				return r;
			}
			target->env_ipc_perm = perm;
		}
		else {
			// 目标不愿意接收
			target->env_ipc_perm = 0; 
		}
	}
	else {
		// 发送方不想发页
		target->env_ipc_perm = 0;
	}

	// 正式塞入数据
	target->env_ipc_value = value;
	target->env_ipc_from = curenv->env_id;
	
	// 唤醒目标进程
	target->env_ipc_recving = 0;
	target->env_status = ENV_RUNNABLE;
	
	// 篡改目标进程的寄存器现场，让它醒来后收到返回值 0
	target->env_tf.tf_regs.reg_eax = 0;
	return 0;
	// panic("sys_ipc_try_send not implemented");
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	// 校验 dstva，如果它低于 UTOP，那它必须是页对齐的。
	if ((uint32_t)dstva < UTOP && (uint32_t)dstva % PGSIZE != 0) {
		return -E_INVAL;
	}

	curenv->env_ipc_recving = 1;
	curenv->env_ipc_dstva = dstva;
	
	// 把自己标记为不可运行，让出 CPU
	curenv->env_status = ENV_NOT_RUNNABLE;
	
	// 跳转到调度器，放弃当前时间片。当以后被发送方唤醒时，发送方会负责修改我们的 Trapframe 的 eax 为 0。
	sched_yield();
	// panic("sys_ipc_recv not implemented");
	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	// panic("sys_time_msec not implemented");
	return time_msec();
}

static int
sys_pkt_send(void *buf, size_t len)
{
	// 检查内存是否属于用户空间，并且用户可读
	user_mem_assert(curenv, buf, len, PTE_U);
	return e1000_transmit(buf, len);
}

static int
sys_pkt_recv(void *buf, size_t len)
{
	// 内核要往 buf 写数据，得有 PTE_W
	user_mem_assert(curenv, buf, len, PTE_U | PTE_W);
	return e1000_receive(buf, len);
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");

	switch (syscallno) {
		case SYS_cputs:
			sys_cputs((const char *)a1, a2);
			return 0;

		case SYS_cgetc:
			return sys_cgetc();

		case SYS_getenvid:
			return sys_getenvid();

		case SYS_env_destroy:
			return sys_env_destroy(a1);

		case SYS_yield:
			sys_yield();
			return 0;

		case SYS_exofork:
			return sys_exofork();

		case SYS_env_set_status:
			return sys_env_set_status((envid_t)a1, (int)a2);

		case SYS_page_alloc:
			return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);

		case SYS_page_map:
			return sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);

		case SYS_page_unmap:
			return sys_page_unmap((envid_t)a1, (void *)a2);
			
		case SYS_env_set_pgfault_upcall:
			return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);

		case SYS_ipc_try_send:
			return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);

		case SYS_ipc_recv:
			return sys_ipc_recv((void *)a1);
			
		case SYS_env_set_trapframe:
			return sys_env_set_trapframe(a1, (struct Trapframe *)a2);
			
		case SYS_time_msec:
			return sys_time_msec();

		case SYS_pkt_send:
			return sys_pkt_send((void *)a1, (size_t)a2);

		case SYS_pkt_recv:
			return sys_pkt_recv((void *)a1, (size_t)a2);
			
		default:
			return -E_INVAL;
	}
}

