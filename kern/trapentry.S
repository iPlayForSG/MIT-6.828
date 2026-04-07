/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

###################################################################
# Data section for the array of trap entry points
###################################################################
.data
.p2align 2
.globl trap_entries
trap_entries:

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
	.text; \
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps; \
	.data; \
	.long name       /* 将当前入口标签的地址追加到 .data 段的数组中 */

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.text; \
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps; \
	.data; \
	.long name

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
TRAPHANDLER_NOEC(divide_entry, T_DIVIDE)    /* 0 */
TRAPHANDLER_NOEC(debug_entry, T_DEBUG)      /* 1 */
TRAPHANDLER_NOEC(nmi_entry, T_NMI)          /* 2 */
TRAPHANDLER_NOEC(brkpt_entry, T_BRKPT)      /* 3 */
TRAPHANDLER_NOEC(oflow_entry, T_OFLOW)      /* 4 */
TRAPHANDLER_NOEC(bound_entry, T_BOUND)      /* 5 */
TRAPHANDLER_NOEC(illop_entry, T_ILLOP)      /* 6 */
TRAPHANDLER_NOEC(device_entry, T_DEVICE)    /* 7 */
TRAPHANDLER(dblflt_entry, T_DBLFLT)         /* 8 */
TRAPHANDLER_NOEC(reserved9_entry, 9)        /* 9 (Reserved) */
TRAPHANDLER(tss_entry, T_TSS)               /* 10 */
TRAPHANDLER(segnp_entry, T_SEGNP)           /* 11 */
TRAPHANDLER(stack_entry, T_STACK)           /* 12 */
TRAPHANDLER(gpflt_entry, T_GPFLT)           /* 13 */
TRAPHANDLER(pgflt_entry, T_PGFLT)           /* 14 */
TRAPHANDLER_NOEC(reserved15_entry, 15)      /* 15 (Reserved) */
TRAPHANDLER_NOEC(fperr_entry, T_FPERR)      /* 16 */
TRAPHANDLER(align_entry, T_ALIGN)           /* 17 */
TRAPHANDLER_NOEC(mchk_entry, T_MCHK)        /* 18 */
TRAPHANDLER_NOEC(simderr_entry, T_SIMDERR)  /* 19 */
TRAPHANDLER_NOEC(reserved20_entry, 20)
TRAPHANDLER_NOEC(reserved21_entry, 21)
TRAPHANDLER_NOEC(reserved22_entry, 22)
TRAPHANDLER_NOEC(reserved23_entry, 23)
TRAPHANDLER_NOEC(reserved24_entry, 24)
TRAPHANDLER_NOEC(reserved25_entry, 25)
TRAPHANDLER_NOEC(reserved26_entry, 26)
TRAPHANDLER_NOEC(reserved27_entry, 27)
TRAPHANDLER_NOEC(reserved28_entry, 28)
TRAPHANDLER_NOEC(reserved29_entry, 29)
TRAPHANDLER_NOEC(reserved30_entry, 30)
TRAPHANDLER_NOEC(reserved31_entry, 31)
TRAPHANDLER_NOEC(syscall_entry, T_SYSCALL)


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
