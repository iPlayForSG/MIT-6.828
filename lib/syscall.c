// System call stubs.

#include <inc/syscall.h>
#include <inc/lib.h>

static inline int32_t
syscall(int num, int check, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	int32_t ret;

	// Generic system call: pass system call number in AX,
	// up to five parameters in DX, CX, BX, DI, SI.
	// Interrupt kernel with T_SYSCALL.
	//
	// The "volatile" tells the assembler not to optimize
	// this instruction away just because we don't use the
	// return value.
	//
	// The last clause tells the assembler that this can
	// potentially change the condition codes and arbitrary
	// memory locations.

	asm volatile("int %1\n"
		     : "=a" (ret)
		     : "i" (T_SYSCALL),
		       "a" (num),
		       "d" (a1),
		       "c" (a2),
		       "b" (a3),
		       "D" (a4),
		       "S" (a5)
		     : "cc", "memory");
/* My Attempt at Lab 3 Challenge 3
	asm volatile(
            "pushl %%ebp\n\t"        // 备份 %ebp，用它传参
            "movl %%esp, %%ebp\n\t"  // 将返回时的 esp 存入 %ebp
            "leal 1f, %%esi\n\t"     // 将 sysenter 后的下一条指令的地址存入 %esi
            "sysenter\n\t"           // 进入内核
            "1:\n\t"                 // sysexit 跳回此处
            "popl %%ebp\n\t"         // 恢复 %ebp
            : "=a" (ret)
            : "a" (num), "d" (a1), "c" (a2), "b" (a3), "D" (a4)
            : "cc", "memory", "esi"  // 声明被破坏的寄存器
    );
*/
	if(check && ret > 0)
		panic("syscall %d returned %d (> 0)", num, ret);

	return ret;
}

void
sys_cputs(const char *s, size_t len)
{
	syscall(SYS_cputs, 0, (uint32_t)s, len, 0, 0, 0);
}

int
sys_cgetc(void)
{
	return syscall(SYS_cgetc, 0, 0, 0, 0, 0, 0);
}

int
sys_env_destroy(envid_t envid)
{
	return syscall(SYS_env_destroy, 1, envid, 0, 0, 0, 0);
}

envid_t
sys_getenvid(void)
{
	 return syscall(SYS_getenvid, 0, 0, 0, 0, 0, 0);
}

