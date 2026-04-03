/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(divide_entry, T_DIVIDE)
TRAPHANDLER_NOEC(debug_entry, T_DEBUG)
TRAPHANDLER_NOEC(nmi_entry, T_NMI)
TRAPHANDLER_NOEC(brkpt_entry, T_BRKPT)
TRAPHANDLER_NOEC(oflow_entry, T_OFLOW)
TRAPHANDLER_NOEC(bound_entry, T_BOUND)
TRAPHANDLER_NOEC(illop_entry, T_ILLOP)
TRAPHANDLER_NOEC(device_entry, T_DEVICE)
TRAPHANDLER(dblflt_entry, T_DBLFLT)
TRAPHANDLER(tss_entry, T_TSS)
TRAPHANDLER(segnp_entry, T_SEGNP)
TRAPHANDLER(stack_entry, T_STACK)
TRAPHANDLER(gpflt_entry, T_GPFLT)
TRAPHANDLER(pgflt_entry, T_PGFLT)
TRAPHANDLER_NOEC(fperr_entry, T_FPERR)
TRAPHANDLER(align_entry, T_ALIGN)
TRAPHANDLER_NOEC(mchk_entry, T_MCHK)
TRAPHANDLER_NOEC(simderr_entry, T_SIMDERR)


/*
 * Lab 3: Your code here for _alltraps
 */

_alltraps:
    # 按照 struct Trapframe 的布局，依次压入剩余的段寄存器和通用寄存器
    pushl %ds
    pushl %es
    pushal

    # 异常发生后，需要确保内核使用的是内核的数据段 GD_KD
    movw $GD_KD, %ax
    movw %ax, %ds
    movw %ax, %es

    # 将当前栈顶指针，也就是 Trapframe 的起始地址作为参数压栈
    # 传递给 C 函数 void trap(struct Trapframe *tf)
    pushl %esp

    # 将控制权移交给 C 的处理逻辑
    call trap
