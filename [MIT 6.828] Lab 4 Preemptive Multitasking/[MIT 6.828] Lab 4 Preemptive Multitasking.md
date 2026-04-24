这个 Lab 的目标是实现抢占式多任务处理，要处理很多同步和调度问题。

# Part A: Multiprocessor Support and Cooperative Multitasking

在本实验的第一部分，您将首先扩展 JOS 以在多处理器系统上运行，然后实现一些新的 JOS 内核系统调用，以允许用户级环境创建额外的新环境。您还将实现协作式的轮转调度，允许内核在当前环境自愿放弃 CPU（或退出）时，从一个环境切换到另一个环境。稍后在 C 部分中，您将实现抢占式调度，它允许内核在经过一定时间后重新夺回 CPU 控制权，即使该环境并不配合。

## Multiprocessor Support

我们将使 JOS 支持“对称多处理”（SMP），这是一种多处理器模型，其中所有 CPU 对系统资源（如内存和 I/O 总线）具有同等的访问权限。虽然在 SMP 中所有 CPU 的功能是相同的，但在引导过程中它们可以分为两类：

* **引导处理器 (BSP, bootstrap processor)**：负责初始化系统和引导操作系统。

* **应用处理器 (APs, application processors)**：仅在操作系统启动并运行后才由 BSP 激活。

哪个处理器是 BSP 由硬件和 BIOS 决定。到目前为止，您现有的所有 JOS 代码都在 BSP 上运行。

在 SMP 系统中，每个 CPU 都有一个配套的本地 APIC (LAPIC) 单元。LAPIC 单元负责在整个系统中传递中断。LAPIC 还为其连接的 CPU 提供唯一的标识符。在本实验中，我们利用了 LAPIC 单元的以下基本功能（在 `kern/lapic.c` 中）：

* 读取 LAPIC 标识符 (APIC ID) 来判断我们的代码当前在哪个 CPU 上运行（参见 `cpunum()`）。

* 将 `STARTUP` 处理器间中断 (IPI) 从 BSP 发送到 AP 以启动其他 CPU（参见 `lapic_startap()`）。

* 在 C 部分中，我们对方 LAPIC 的内置定时器进行编程，以触发时钟中断以支持抢占式多任务处理（参见 `apic_init()`）。

处理器使用内存映射 I/O (MMIO) 访问其 LAPIC。在 MMIO 中，物理内存的一部分硬连线到某些 I/O 设备的寄存器，因此通常用于访问内存的相同加载/存储指令也可用于访问设备寄存器。您已经看到过物理地址 `0xA0000` 处的一个 IO 空洞（我们用它来写入 VGA 显示缓冲区）。LAPIC 位于从物理地址 `0xFE000000`（差 32MB 到 4GB）开始的空洞中，因此它太高了，我们无法使用 `KERNBASE` 处通常的直接映射来访问它。JOS 虚拟内存映射在 `MMIOBASE` 处留下了 4MB 的间隙，因此我们有地方映射这样的设备。由于后面的实验引入了更多的 MMIO 区域，您将编写一个简单的函数来从该区域分配空间并将设备内存映射到该空间。

### Exercise 1

> 在 `kern/pmap.c` 中实现 `mmio_map_region`。要了解其使用方法，请查看 `kern/lapic.c` 中 `lapic_init` 的开头。您还必须完成下一个练习，然后才能运行 `mmio_map_region` 的测试。

在 Lab 1 的 The PC's Physical Address Space 里提到过，操作 VGA 显示缓冲区这种外设时，我们是直接去访问低地址的物理洞，比如 0xA0000 这样。在现代操作系统中，每个 CPU 都有一个本地中断控制器，也就是 Local APIC 。它的物理地址被硬件定死在 0xFE000000 附近，离 4GB 的上限很近。

讲义也提到了，这个地址非常高，超出了我们之前在 KERNBASE 直接映射的物理内存范围。JOS 为了解决这个问题，在虚拟地址空间里专门留了一块 4MB 的区域： MMIOBASE 到 MMIOLIM。这块区域就是用来映射各种内存映射 I/O (MMIO) 设备的。

根据注释的提示，mmio_map_region 就是给定一个物理地址 pa 和大小 size，在这个 MMIOBASE 区域里拿一块出来，对齐到页边界，再用之前写过的 boot_map_region 建立映射，然后返回这段虚拟空间的起始地址。

由于设备内存跟 DRAM 不同，我们不能让 CPU 去缓存对外设寄存器的读写操作，否则读到的就是缓存里的旧数据，发出的指令外设也收不到。所以映射的时候除了常规的 PTE_W 和 PTE_P，还要加上缓存禁用标志位 PTE_PCD (Cache-Disable) 和 PTE_PWT (Write-Through)。

```c
void *
mmio_map_region(physaddr_t pa, size_t size)
{
	static uintptr_t base = MMIOBASE;

	// Your code here:
	size = ROUNDUP(size, PGSIZE);
	if (base + size > MMIOLIM) {
		panic("mmio_map_region: MMIO region overflow");
	}

	boot_map_region(kern_pgdir, base, size, ROUNDDOWN(pa, PGSIZE), PTE_W | PTE_PCD | PTE_PWT);
	uintptr_t ret_va = base;
	base += size;

	return (void *) ret_va;
	// panic("mmio_map_region not implemented");
}
```

## Application Processor Bootstrap

在启动 AP 之前，BSP 应首先收集有关多处理器系统的信息，例如 CPU 总数、它们的 APIC ID 以及 LAPIC 单元的 MMIO 地址。`kern/mpconfig.c` 中的 `mp_init()` 函数通过读取位于 BIOS 内存区域中的 MP 配置表来检索此信息。

`boot_aps()` 函数（在 `kern/init.c` 中）驱动 AP 引导过程。AP 在实模式下启动，非常类似于 bootloader 在 `boot/boot.S` 中的启动方式，因此 `boot_aps()` 将 AP 入口代码 (`kern/mpentry.S`) 复制到实模式下可寻址的内存位置。与 bootloader 不同，我们可以控制 AP 开始执行代码的位置；我们将入口代码复制到 `0x7000` (`MPENTRY_PADDR`)，但低于 640KB 的任何未使用且页面对齐的物理地址都可以使用。

之后，`boot_aps()` 通过向相应 AP 的 LAPIC 单元发送 `STARTUP` IPI 来逐个激活 AP，以及 AP 应开始运行其入口代码（在我们的例子中为 `MPENTRY_PADDR`）的初始 `CS:IP` 地址。`kern/mpentry.S` 中的入口代码与 `boot/boot.S` 的代码非常相似。经过简短的设置后，它将 AP 置于启用了分页的保护模式，然后调用 C 设置例程 `mp_main()`（也在 `kern/init.c` 中）。`boot_aps()` 等待 AP 在其 `struct CpuInfo` 的 `cpu_status` 字段中发出 `CPU_STARTED` 标志信号，然后再继续唤醒下一个。

### Exercise 2

> 阅读 `kern/init.c` 中的 `boot_aps()` 和 `mp_main()`，以及 `kern/mpentry.S` 中的汇编代码。确保您理解 AP 引导期间的控制流传输。然后修改您在 `kern/pmap.c` 中 `page_init()` 的实现，以避免将 `MPENTRY_PADDR` 处的页面添加到空闲列表中，这样我们就可以安全地复制并在该物理地址运行 AP 引导代码。您的代码应通过更新后的 `check_page_free_list()` 测试（但可能无法通过更新后的 `check_kern_pgdir()` 测试，我们将很快修复该问题）。

这里要做的就是唤醒其它的 CPU (APs)，这跟系统刚上电时 BSP 的启动过程类似，AP 刚启动时也是处于 16 位的实模式。

实模式有一个限制：它只能寻址最低的 1MB 物理内存。我们的内核通常加载在 1MB 以上的高地址，AP 根本够不着。所以，BSP 必须得把 AP 的启动代码 kern/mpentry.S 复制到 1MB 以下的某个安全的物理地址去。JOS 选中的物理地址是 MPENTRY_PADDR = 0x7000

注意 page_init() 的注释。回想一下，之前在 Lab 2 里面写 page_init() 时，把 1MB 以下的可用内存全扔进 page_free_list 里了。如果按原来的做法，0x7000 这个物理页随时可能被内核或者用户程序申请走，那 AP 启动代码就会被覆写，等 AP 准备执行时，就只有一堆乱码了。所以，得先把 MPENTRY_PADDR 所在的物理页提出来，不加入空闲链表。

```c
// Lab 4 Exercise 2: 保留 MPENTRY_PADDR 这个物理页，防止 AP 启动代码被覆盖
if (i == PGNUM(MPENTRY_PADDR)) {
    pages[i].pp_ref = 1;
    pages[i].pp_link = NULL;
}
```

现在 check_page_free_list 过了，但是`kernel panic on CPU 0 at kern/pmap.c:954: assertion failed: check_va2pa(pgdir, base + KSTKGAP + i) == PADDR(percpu_kstacks[n]) + i`，也就是 check_kern_pgdir 没过，符合题目的叙述。

### Questions 1

> 1. 将 `kern/mpentry.S` 与 `boot/boot.S` 并排比较。请记住，`kern/mpentry.S` 经过编译和链接，可以在 `KERNBASE` 之上运行，就像内核中的所有其他代码一样，宏 `MPBOOTPHYS` 的作用是什么？为什么它在 `kern/mpentry.S` 中是必需的，而在 `boot/boot.S` 中却不需要？换句话说，如果在 `kern/mpentry.S` 中省略它，可能会出什么问题？
>
>  *提示：回想一下我们在实验 1 中讨论过的链接地址和加载地址之间的区别。*

bootloader 的代码的链接地址和加载地址是一致的，都在 0x7C00。编译器在编译 boot.S 时计算出来的符号地址是物理内存中 0x7C00 附近的地址，所以可以运行时直接取址。

mpentry.S 是跟内核一起编译的，它的链接地址在虚拟高地址 KERNBASE + offset，也就是说编译器会认为这段代码将来会运行在 0xF010 0000 这种地址上。

但是，AP 在刚启动时是处于实模式，没有开启分页机制，没有虚拟地址转换，而且这段代码在物理地址 0x7000，这就导致了加载地址和链接地址脱节。

如果我们在 mpentry.S 里直接用一个符号，比如用指令把 GDT 的地址加载到寄存器，编译器填入的会是一个 0xF01xxxxx 的高地址。处于实模式的 AP 拿到这个地址根本无法访问，直接报错。

所以，MPBOOTPHYS 的作用就是在运行时做重定位。

我们可以看一下 mpentry.S 里这个宏的定义：

```c
#define MPBOOTPHYS(s) ((s) - mpentry_start + MPENTRY_PADDR)
```

这段代码的意思是：把符号 s 的链接地址，减去这段代码的起始链接地址 mpentry_start，得出该符号在这段代码中的相对偏移，然后再把这个偏移加上实际的加载首地址 MPENTRY_PADDR。

这样一算，AP 就能算出当前实模式下它应该去访问的具体物理地址了。如果省略这个宏，AP 就会用链接时的高地址去寻址物理内存，实模式下寻址超限，抛出异常。

## Per-CPU State and Initialization

在编写多处理器操作系统时，区分每个处理器私有的每 CPU (per-CPU) 状态与整个系统共享的全局状态非常重要。`kern/cpu.h` 定义了大部分每 CPU 状态，包括 `struct CpuInfo`，它存储了每 CPU 变量。`cpunum()` 总是返回调用它的 CPU 的 ID，它可以作为像 `cpus` 这样的数组的索引。或者，宏 `thiscpu` 是当前 CPU 的 `struct CpuInfo` 的简写。

以下是您应该了解的每 CPU 状态：

- **每 CPU 内核栈 (Per-CPU kernel stack)：** 因为多个 CPU 可以同时陷入（trap）内核，我们需要为每个处理器分配一个独立的内核栈，以防止它们干扰彼此的执行。
- **每 CPU TSS 和 TSS 描述符：** 需要每 CPU 任务状态段 (TSS) 来指定每个 CPU 的内核栈所在的位置。
- **每 CPU 当前环境指针：** 由于每个 CPU 可以同时运行不同的用户进程，我们将符号 `curenv` 重新定义为引用 `cpus[cpunum()].cpu_env`。
- **每 CPU 系统寄存器：** 所有寄存器（包括系统寄存器）都是 CPU 私有的。必须在每个 CPU 上执行初始化这些寄存器的指令。

### Exercise 3

> 修改 `mem_init_mp()`（在 `kern/pmap.c` 中），按照 `inc/memlayout.h` 中所示的布局，将每个 CPU 的栈映射到从 `KSTACKTOP` 开始的区域。每个栈的大小是 `KSTKSIZE` 字节，外加 `KSTKGAP` 字节的未映射保护页（guard pages）。你的代码应该能通过 `check_kern_pgdir()` 中的新检查。

在之前的单处理器 lab 里我们只在 mem_init 映射了一个内核栈，在 KSTACKTOP 下方。现在是多处理器，就不能共用同一个内核栈了。JOS 定义了一个 `percpu_kstacks[NCPU][KSTKSIZE]`，用于为每个 CPU 分配内核栈。

memlayout.h 中会发现，JOS 在两个相邻内核栈中插入了一个 KSTKGAP，这片区域不做映射也不分配内存，作为保护页。一旦进入该页，CPU 会立刻抛出 Page Fault。

对于第 i 个 CPU，它的栈顶是 `kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP)`，需要被映射的地址是`kstacktop_i - KSTKSIZE`

```c
int i;
for (i = 0; i < NCPU; i++) {
    uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);

    uintptr_t va = kstacktop_i - KSTKSIZE;
    physaddr_t pa = PADDR(percpu_kstacks[i]);

    boot_map_region(kern_pgdir, va, KSTKSIZE, pa, PTE_W | PTE_P);

    // [va - KSTKGAP, va) 保护页，不映射
}
```

现在就能通过 check_kern_pgdir() succeeded! 了。但是依然有 kernel panic: `kernel panic on CPU 0 at kern/trap.c:315: page fault in kernel mode, fault_va: 0x00000000`

### Exercise 4

> `kern/trap.c` 中的 `trap_init_percpu()` 里的代码初始化了 BSP 的 TSS (任务状态段) 和 TSS 描述符。这在 Lab 3 里没问题，但在其他 CPU 上运行就不对了。修改这段代码，使其能在所有 CPU 上工作。（注意：你的新代码不应该再使用全局的 `ts` 变量了。）

Exercise 3 里也看见，trap.c 中 kernel panic 了。在 Lab 3 中，JOS 定义了一个全局变量 `struct Taskstate ts`，这个变量就是 CPU 的 TSS。我们知道，CPU 从 R3 到 R0 时所需的内核栈指针 ESP0 和 SS0 就是存在 TSS 里的。

现在是 SMP 架构，跟 Exercise 3 一样，如果还共用一个 TSS 就会出问题。现在我们就要为每个 CPU 都分配 TSS。kern/cpu.h 中有 `struct CpuInfo`，里面的 cpu_ts 字段就是我们要用的东西。根据 Lab 4 的注释写即可

```c
int i = cpunum();
// ts_esp0 指向该 CPU 专属的内核栈栈底
thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
thiscpu->cpu_ts.ts_ss0 = GD_KD;
// ts_iomb 设为结构体的大小，防止用户态通过 IO bitmap 越权访问端口
thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

gdt[(GD_TSS0 >> 3) + i] = SEG16(STS_T32A, (uint32_t) (&(thiscpu->cpu_ts)),
                                sizeof(struct Taskstate) - 1, 0);
gdt[(GD_TSS0 >> 3) + i].sd_s = 0;
// GD_TSS0 是第 0 个 CPU 的选择子，每个选择子占 8 字节，所以要偏移 (i << 3)
ltr(GD_TSS0 + (i << 3));
// 虽然 IDT 全系统共用，但每个 CPU 的 IDTR 寄存器是私有的，所以每个 CPU 都要加载一次
lidt(&idt_pd);
```

然后回发现 make qemu CPUS=4 会直接 Triple Fault。

```shell
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make qemu CPUS=4
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 4 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
CPU Supports PSE.
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
EAX=00270000 EBX=00000000 ECX=00000000 EDX=00000663
ESI=00000000 EDI=00000000 EBP=f023eff8 ESP=f023eff0
EIP=f01001c0 EFL=00000007 [-----PC] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
CS =0008 00000000 ffffffff 00cf9a00 DPL=0 CS32 [-R-]
SS =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
DS =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
FS =0000 00000000 00000000 00000000
GS =0000 00000000 00000000 00000000
LDT=0000 00000000 0000ffff 00008200 DPL=0 LDT
TR =0000 00000000 0000ffff 00008b00 DPL=0 TSS32-busy
GDT=     0000705c 00000017
IDT=     00000000 0000ffff
CR0=e0010011 CR2=00000040 CR3=00270000 CR4=00000000
DR0=00000000 DR1=00000000 DR2=00000000 DR3=00000000 
DR6=ffff0ff0 DR7=00000400
EFER=0000000000000000
Triple fault.  Halting for inspection via QEMU monitor.
QEMU: Terminated
```

调试了一段时间发现，主要原因是 AP 的 CR4 没有开 PSE。 kern_pgdir 在 Lab 2 challenge 里用了 4MB PTE_PS 映射，BSP 在 mem_init() 里开了 CR4_PSE，但 CR4 是 per-CPU 的，AP 启动时仍然是 CR4=0。而且，QEMU dump 里也能看到 CR4=00000000，而 EIP=f01001c0 对应 mp_main() 里 `lcr3(PADDR(kern_pgdir))` 后的第一条调用。AP 切到含 PSE 映射的 kern_pgdir 后无法正常取指/处理异常，又还没 lidt()，所以 triple fault 了。

那就在 kern/init 的 mp_main() 开头加个`lcr4(rcr4() | CR4_PSE);`

```shell
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 2 starting
SMP: CPU 3 starting
[00000000] new env 00001000
kernel panic on CPU 0 at kern/trap.c:312: page fault in kernel mode, fault_va: 0x00000000
```

现在好了，后面这个 page fault 是后面要解决的问题。我已经预感到后面还会被之前的 Challenge 坑几次..

---

完成以上练习后，在 QEMU 中使用 4 个 CPU 运行 JOS，命令为 `make qemu CPUS=4`（或 `make qemu-nox CPUS=4`），你应该会看到类似这样的输出：

```
Physical memory: 66556K available, base = 640K, extended = 65532K
check_page_alloc() succeeded!
check_page() succeeded!
check_kern_pgdir() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 2 starting
SMP: CPU 3 starting
```

## Locking

我们当前的代码在 `mp_main()` 中初始化 AP 后自旋。在让 AP 进一步运行之前，我们需要首先解决当多个 CPU 同时运行内核代码时的竞争条件。实现此目的的最简单方法是使用**大内核锁 (big kernel lock)**。

您应该在四个位置应用大内核锁：

1. 在 `i386_init()` 中，在 BSP 唤醒其他 CPU 之前获取锁。
2. 在 `mp_main()` 中，在初始化 AP 后获取锁。
3. 在 `trap()` 中，当从用户模式陷入时获取锁。
4. 在 `env_run()` 中，在切换到用户模式的**正前方**释放锁。

### Exercise 5

> 按照上述描述应用大内核锁，通过在正确的位置调用 `lock_kernel()` 和 `unlock_kernel()`。

之前我们把多核唤醒了，也配了 TSS 和 Kernel Stack，但是现在还有一些共享的内核数据，比如说 env_free_list 和 page_free_list 之类的。如果多个 CPU 同时往里写就会出问题。

大内核锁就是干这个事情的。不管有几个 CPU，只要想进内核态，就得先拿到这个锁，有锁的进内核，没有的就自旋等待。这个方法虽然把内核态的并发性能退化成单核，但是胜在安全，而且好实现。

根据讲义，首先是 i386_init()，在唤醒其它 AP 之前就抢占锁

```c
// Acquire the big kernel lock before waking up APs
// Your code here:
lock_kernel();
// Starting non-boot CPUs
boot_aps();
```

然后 mp_main()，AP 做完属于自己的硬件级初始化后，马上进入调度器去挑选进程运行。在这之前，必须排队拿锁。

```c
...
lock_kernel();
sched_yield(); // 进入调度器去挑进程跑
// Remove this after you finish Exercise 6
// for (;;);
```

trap()，这里是所有的中断和异常陷入内核的统一入口。需要注意：只有从用户态陷入内核时才需要加锁。

如果当前已经是在内核态了，比如内核里发生了页错误，或者嵌套中断，那说明当前 CPU 已经占有大内核锁了。如果我们再加一次锁，因为我们这里的 Spinlock 是不可重入的，CPU 就会自己把自己死锁，永远卡在那里。 

检查 tf->tf_cs 的低两位（CPL）是否为 3，就能判断是不是从用户态进来的。

```c
if ((tf->tf_cs & 3) == 3) {
    // Trapped from user mode.
    // Acquire the big kernel lock before doing any
    // serious kernel work.
    // LAB 4: Your code here.
    lock_kernel();
    assert(curenv);
    ...
```

env_run()，当调度器选好了一个进程，准备把它跑起来（恢复它的寄存器上下文并退回用户态）的时候，就要释放锁了。

```c
...
lcr3(PADDR(curenv->env_pgdir));
unlock_kernel();
env_pop_tf(&curenv->env_tf);
```

现在 make qemu CPUS=4

```shell
SMP: CPU 0 found 4 CPU(s)
enabled interrupts: 1 2
SMP: CPU 1 starting
SMP: CPU 2 starting
SMP: CPU 3 starting
[00000000] new env 00001000
QEMU: Terminated
```

发现阻塞在了 new env 这里，这是正常的，我们在 mp_main 这里提前写了个 sched_yield()，这是 Exercise 6 会用到的妙妙工具。

### Question 2

> 2. 似乎使用大内核锁保证了同一时间只有一个 CPU 可以运行内核代码。那为什么我们仍然需要为每个 CPU 分配单独的内核栈？请描述一个在共享内核栈的情况下会出错的场景，即使有大内核锁的保护。

这个锁是在软件层面的， 而中断压栈是在硬件层面的。

假设我们所有的 CPU 都共享同一个物理内核栈，在同一个 TSS 里配置相同的 ESP0。 现在 CPU 0 和 CPU 1 都在运行各自的用户态程序。突然，它们在同一个极短的时间内同时发生了时钟中断。那么硬件就会响应：

1. CPU 0 发现中断，立刻切换特权级，读取 TSS 找到共享内核栈的 ESP0
2. CPU 1 发现中断，也立刻切换特权级，读取 TSS 找到同一个共享内核栈的 ESP0
3. 两颗 CPU 的硬件电路开始同时往这个相同的内存地址压入各自 Trapframe 中的寄存器

这个时候，任何软件层面的 lock_kernel 都来不及执行。结果就是 CPU 0 和 CPU 1 的上下文数据互相覆盖，最终试图 iret 恢复环境时直接 panic

所以，每个 CPU 必须有自己私有的内核栈，用来接收硬件自动压入的第一波中断上下文。 等把现场安全保存到私有栈上之后，再去竞争大内核锁。

---

Challenge 跟之前的 Lab 冲突，把我整个环境炸了几次，已红温回滚。

Lab 4 开始的 Challenge 暂时不做了，做完整个 6.828 再补

---

## Round-Robin Scheduling

您在本实验中的下一个任务是更改 JOS 内核，以便它可以以“轮转（round-robin）”的方式在多个环境之间交替。

- 新的 `kern/sched.c` 中的函数 `sched_yield()` 负责选择要运行的新环境。它以循环方式顺序搜索 `envs[]` 数组。
- `sched_yield()` 绝不能同时在两个 CPU 上运行相同的环境。
- 我们为您实现了一个新的系统调用 `sys_yield()`，用户环境可以调用它来调用内核的 `sched_yield()`。

### Exercise 6

> 如上所述，在 `sched_yield()` 中实现轮询调度。别忘了修改 `syscall()` 来调用 `sys_yield()`。
>
> 确保在 `mp_main` 中调用 `sched_yield()`。
>
> 修改 `kern/init.c`，创建三个（或更多！）运行 `user/yield.c` 程序的环境。
>
> 运行 `make qemu`。你应该会看到这些环境在终止前来回切换五次，如下图所示。
>
> 也请使用多个 CPU 进行测试：`make qemu CPUS=2`。
>
> ```
> Hello, I am environment 00001000.
> Hello, I am environment 00001001.
> Hello, I am environment 00001002.
> Back in environment 00001000, iteration 0.
> Back in environment 00001001, iteration 0.
> Back in environment 00001002, iteration 0.
> Back in environment 00001000, iteration 1.
> Back in environment 00001001, iteration 1.
> Back in environment 00001002, iteration 1.
> ```
>
> yield 程序退出后，系统中将没有可运行的环境，调度器应该调用 JOS 内核监视器。如果上述任何一项没有发生，请在继续操作之前修复您的代码。

根据注释，一个想法是直接写一个环形队列，接着上一个跑完的进程往下找，找到第一个状态是 ENV_RUNNABLE 的进程就切换过去；如果没找到，但是自己还能跑，就接着跑自己；如果自己也没得跑，就 sched_halt 休眠。

sched_yield 给的 `struct Env *idle` 完全没用到，直接注释掉

```c
// LAB 4: Your code here.
int start = 0;
int i;

// 如果当前有正在运行的环境，从它的下一个开始寻找
if (curenv) {
    start = ENVX(curenv->env_id) + 1;
}

// 最多查找 NENV 次
for (i = 0; i < NENV; i++) {
    int idx = (start + i) % NENV;
    if (envs[idx].env_status == ENV_RUNNABLE) {
        env_run(&envs[idx]);
        // env_run 会直接切入用户态，永远不会返回
    }
}

// 如果没有其他可运行的环境，但当前环境依然可以运行，那就接着跑自己
if (curenv && curenv->env_status == ENV_RUNNING) {
    env_run(curenv);
}
// sched_halt never returns
sched_halt();
```

记得在 kern/syscall.c 的 syscall() 注册分支

```c
case SYS_yield:
    sys_yield();
    return 0;
```

kern/init.c 里把 user_primes 注释掉，之后 make qemu CPUS=2 就能看到几个 CPU 交替接管进程了。

```c
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	// ENV_CREATE(user_primes, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
	ENV_CREATE(user_yield, ENV_TYPE_USER);
```

![[MIT 6.828 Lab 4] 1](F:\Desktop\My Blog's Backup\[MIT 6.828] Lab 4 Preemptive Multitasking\img\[MIT 6.828 Lab 4] 1.png)

### Question 3

> 3. 在你实现的 `env_run()` 中，你应该调用了 `lcr3()`。在调用 `lcr3()` 的前后，你的代码都引用了变量 `e`（`env_run` 的参数）。加载 `%cr3` 寄存器后，MMU 使用的寻址上下文瞬间改变了。但是虚拟地址（即 `e`）是有意义的，它相对于给定的地址上下文——地址上下文指定了虚拟地址映射到的物理地址。为什么指针 `e` 在地址切换前后都可以被解引用？
> 4. 每当内核从一个环境切换到另一个环境时，它必须确保旧环境的寄存器被保存，以便稍后能够正确恢复。为什么？这发生在代码的什么地方？

3. e 是一个指向 struct Env 的指针，它指向的是内核中分配好的 envs 数组里的某个元素。envs 数组所在的内存区域位于 UTOP 之上的 KERNBASE 区域，是由内核在 mem_init() 中映射好的。

   我们在创建任何一个新的用户环境 env_setup_vm 时，都会把内核页目录 kern_pgdir 中关于 UTOP 以上的内核映射，原封不动地复制给这个新环境的页目录。也就是说，不管是旧环境的页目录，还是新环境 e 的页目录，它们在虚拟地址 UTOP 以上的部分是完全一模一样的。因此，当 lcr3 切换了物理页目录基址后，虽然底层的页目录表换了，但针对指针 e 所在的虚拟高地址段，MMU 查表得出的物理地址依然是同一个。所以 e 在切页表前后随便解引用，不会 Page Fault。

4. 为什么？因为操作系统的并发可以理解为一种时间切片，当进程 A 正在进行运算时，时间片耗尽或者主动 yield 后，CPU 的占有权被剥夺，如果不把此时 A 的讲清楚保存下来，下次再切回 A 时，就不知道它之前做了什么运算、做到哪一步了。

   在哪发生？首先是硬件，当进程中断或者系统调用的时候，CPU 硬件会自动把 SS、ESP、EFLAGS、CS、EIP  等寄存器压入当前 CPU 专属的内核栈中；然后是软件，比如 JOS 的 kern/trapentry.S 中 的 _alltraps，汇编会把剩下的所有段寄存器和通用寄存器压入栈中，保存为 struct Trapframe；最后是内核，kern/trap.c 的 trap() 中有 `curenv->env_tf = *tf;`，这句代码把压在内核栈上的整个 Trapframe 都复制到了当前进程的 Env 结构体里面保存。

## System Calls for Environment Creation

虽然你的内核现在能够运行并在多个用户级环境之间切换，但它仍然局限于运行内核最初设置的环境。你现在将实现必要的 JOS 系统调用，以允许用户环境创建并启动其他新的用户环境。

Unix 提供了 `fork()` 系统调用作为其进程创建原语。Unix 的 `fork()` 复制调用进程（父进程）的整个地址空间来创建一个新进程（子进程）。从用户空间观察到的两者之间唯一的区别是它们的进程 ID 和父进程 ID（如 `getpid` 和 `getppid` 返回的那样）。在父进程中，`fork()` 返回子进程的进程 ID，而在子进程中，`fork()` 返回 0。默认情况下，每个进程都有自己私有的地址空间，任何一个进程对内存的修改对另一个进程都是不可见的。

你将为创建新的用户模式环境提供一组不同的、更原始的 JOS 系统调用。除了其他风格的环境创建之外，利用这些系统调用，你将能够完全在用户空间中实现类似 Unix 的 `fork()`。你将为 JOS 编写的新系统调用如下：

- `sys_exofork`: 该系统调用创建一个几乎是空白状态的新环境：在其地址空间的用户部分没有任何映射，并且它是不可运行的。新环境将具有与父环境在调用 `sys_exofork` 时的寄存器状态相同的状态。在父进程中，`sys_exofork` 将返回新创建环境的 `envid_t`（如果环境分配失败，则返回负错误码）。然而，在子进程中，它将返回 0。（因为子进程开始时被标记为不可运行，所以在父进程通过使用下文提到的系统调用将其标记为可运行之前，`sys_exofork` 实际上不会在子进程中返回……）
- `sys_env_set_status`: 将指定环境的状态设置为 `ENV_RUNNABLE` 或 `ENV_NOT_RUNNABLE`。这个系统调用通常用于在一个新环境的地址空间和寄存器状态完全初始化后，将其标记为准备好运行。
- `sys_page_alloc`: 分配一页物理内存，并将其映射到给定环境地址空间中的给定虚拟地址。
- `sys_page_map`: 将一个页面映射（而不是页面的内容！）从一个环境的地址空间复制到另一个环境，建立内存共享安排，使得新的和旧的映射都引用同一页物理内存。
- `sys_page_unmap`: 解除映射给定环境中给定虚拟地址处的页面。

对于所有接受环境 ID 的上述系统调用，JOS 内核支持一种约定，即值为 0 表示“当前环境”。这种约定由 `kern/env.c` 中的 `envid2env()` 实现。

我们在测试程序 `user/dumbfork.c` 中提供了一个非常原始的类似 Unix `fork()` 的实现。这个测试程序使用上述系统调用创建并运行一个具有自身地址空间副本的子环境。这两个环境然后像上一个练习中那样使用 `sys_yield` 相互切换。父环境在 10 次迭代后退出，而子环境在 20 次后退出。

### Exercise 7

> 在 `kern/syscall.c` 中实现上述描述的系统调用，并确保 `syscall()` 调用了它们。你需要使用 `kern/pmap.c` 和 `kern/env.c` 中的各种函数，特别是 `envid2env()`。目前，每当你调用 `envid2env()` 时，在其 `checkperm` 参数中传递 1。确保检查任何无效的系统调用参数，在那种情况下返回 `-E_INVAL`。使用 `user/dumbfork` 测试你的 JOS 内核，在继续之前确保它能工作。

在 Unix 类系统中，进程一般用 fork() 创建，这会直接在内核里把父进程的地址空间复制一份给子进程。JOS 是微内核，它的设计思想是：内核只提供最基本的原语系统调用，具体的诸如地址空间复制、写时复制等都交给用户态的代码自己去实现。这就是我们现在要实现的 5 个系统调用。

#### sys_exofork

这个系统调用会创建一个白板子进程。这个子进程除了拥有和父进程一模一样的寄存器状态外，用户空间什么都没有映射，而且处于 ENV_NOT_RUNNABLE 状态。子进程被唤醒后，也是从 sys_exofork 返回。为了让用户态代码区分自己是父进程还是子进程，依据讲义，我们需要把子进程 Trapframe 里的返回值 %eax 设为 0。

```c
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
```

#### sys_env_set_status

用来修改进程的状态，一般是在父进程给子进程配置完内存后，调用这个把它设为 ENV_RUNNABLE。

```c
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
```

#### sys_page_alloc

给目标进程的特定虚拟地址分配一整页全新的物理内存。

这里安全检查非常多。va 必须低于 UTOP 且页对齐；权限 perm 必须包含 PTE_U 和 PTE_P；分配出来的物理页为了防止内核数据泄露，必须强制清零。

```c
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
```

#### sys_page_map

将源进程某个虚拟地址对应的物理页共享映射给目标进程的某个虚拟地址。这是 JOS 实现共享内存和内存复制的系统调用。 

除了和前面一样的对齐、边界、权限检查，还要检查一个特殊的物理逻辑：如果请求以写权限映射，那这个物理页在源进程那里也必须可写，否则用户就能利用这个漏洞把只读内存变成可写的了。

```c
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
```

#### sys_page_unmap

解除目标进程某个虚拟地址的映射关系，这个没啥好说的，直接 page_remove

```c
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
```

最后，把它们注册进 syscall()

```c
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

```

最后把 kern/init.c 的测试代码从 user_yield 换成 `  ENV_CREATE(user_dumbfork, ENV_TYPE_USER);`  

![[MIT 6.828 Lab 4] 2](F:\Desktop\My Blog's Backup\[MIT 6.828] Lab 4 Preemptive Multitasking\img\[MIT 6.828 Lab 4] 2.png)

child 循环 20，parent 循环 10，所以除了交替输出以外 child 还多了 10 个

# Part B: Copy-on-Write Fork

如前所述，Unix 提供 `fork()` 系统调用作为其主要的进程创建原语。`fork()` 系统调用复制调用进程（父进程）的地址空间来创建一个新进程（子进程）。

xv6 Unix 实现 `fork()` 的方法是将父进程页面的所有数据复制到为子进程分配的新页面中。这基本上与 `dumbfork()` 采用的方法相同。将父进程的地址空间复制到子进程中是 `fork()` 操作中最昂贵的部分。

然而，在子进程中调用 `fork()` 后通常会紧接着调用 `exec()`，后者用新程序替换子进程的内存。例如，这通常是 shell 所做的事情。在这种情况下，花费在复制父进程地址空间上的时间在很大程度上是被浪费的，因为子进程在调用 `exec()` 之前使用的内存非常少。

出于这个原因，后来的 Unix 版本利用虚拟内存硬件，允许父进程和子进程共享映射到它们各自地址空间的内存，直到其中一个进程实际修改了它为止。这项技术被称为写时复制 (copy-on-write)。为此，在 `fork()` 时，内核将从父进程向子进程复制地址空间映射，而不是所映射页面的内容，并同时将现在共享的页面标记为只读。当两个进程之一试图向这些共享页面之一写入时，该进程会产生缺页异常 (page fault)。在这一点上，Unix 内核意识到该页面实际上是一个“虚拟”或“写时复制”的副本，因此它为引发故障的进程生成了一个新的、私有的、可写的该页面副本。这样，各个页面的内容直到它们实际被写入时才被真正复制。这种优化使得后面跟着一个 `exec()` 的 `fork()` 便宜得多：子进程在调用 `exec()` 之前可能只需要复制一页（它当前堆栈的页面）。

在这个实验的下一部分中，你将作为一个用户空间库例程来实现一个“适当的”具有写时复制功能的类似 Unix 的 `fork()`。在用户空间中实现 `fork()` 和写时复制支持的好处是，内核保持得更加简单，因此更可能正确。它还允许各个用户模式程序为 `fork()` 定义自己的语义。如果一个程序需要稍有不同的实现（例如，像 `dumbfork()` 那样昂贵的总是复制版本，或者父子进程在之后实际共享内存的版本），它可以轻松提供自己的实现。

## User-level page fault handling

一个用户级的写时复制 `fork()` 需要了解受写保护页面的缺页异常，所以这正是你首先要实现的内容。写时复制只是用户级缺页异常处理众多可能的用途之一。

设置一个地址空间，以便在发生缺页异常时表明需要采取某些行动，这种做法很常见。例如，大多数 Unix 内核最初仅在新进程的栈区域映射单个页面，后来随着进程栈消耗增加并在尚未映射的栈地址上引发缺页异常时，“按需”分配和映射额外的栈页面。一个典型的 Unix 内核必须跟踪在进程空间的每个区域发生缺页异常时应采取什么行动。例如，栈区域的故障通常会分配和映射物理内存的新页面。程序 BSS 区域的故障通常会分配一个新页，用零填充，并映射它。在具有按需分页可执行文件的系统中，文本区域的故障会从磁盘读取相应的二进制文件页面，然后将其映射。

对于内核来说，要跟踪的信息量非常大。与采用传统的 Unix 做法不同，你将决定在用户空间中如何处理每个缺页异常，在那里漏洞造成的破坏较小。这种设计还有一个额外的好处，即允许程序在定义其内存区域方面具有极大的灵活性；稍后你将使用用户级缺页异常处理来映射和访问基于磁盘文件系统中的文件。

## Setting the Page Fault Handler

为了处理自身的缺页异常，用户环境需要向 JOS 内核注册一个缺页异常处理程序入口点 (page fault handler entrypoint)。用户环境通过新的 `sys_env_set_pgfault_upcall` 系统调用注册其缺页异常入口点。我们在 `Env` 结构中添加了一个新成员 `env_pgfault_upcall`，来记录此信息。

### Exercise 8

> 实现 `sys_env_set_pgfault_upcall` 系统调用。在查找目标环境的 ID 时，一定要启用权限检查，因为这是一个“危险”的系统调用。

内核遇到 Page Fault 后，如果发现这是用户态抛出的缺页异常，它得知道该往用户态的哪个函数去跳。

我们在 Part A 的 struct Env 里见过一个字段：env_pgfault_upcall。这个系统调用的任务非常单纯，就是让用户态程序把自己写的 Page Fault 处理函数的函数指针（或者说是入口地址）告诉内核，内核把这个地址保存在 env_pgfault_upcall 里记下来。

直接改 kern/syscall.c，代码非常简单：

```c
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
```

记得放进 syscall 里分发

```c
case SYS_env_set_pgfault_upcall:
	return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
```

## Normal and Exception Stacks in User Environments

在正常执行期间，JOS 中的用户环境将在常规 (normal) 用户栈上运行：其 `ESP` 寄存器开始时指向 `USTACKTOP`，并且它推送的栈数据驻留在包含 `USTACKTOP-PGSIZE` 和 `USTACKTOP-1` 在内的页面上。然而，当在用户模式下发生缺页异常时，内核将在一个不同的栈（即用户异常 (user exception) 栈）上重启用户环境，运行指定的级别缺页异常处理程序。本质上，我们将让 JOS 内核代表用户环境实现自动的“栈切换”，其方式在很大程度上类似于 x86 处理器在从用户模式转移到内核模式时已经代表 JOS 实现的栈切换！

JOS 用户异常栈的大小也是一页，并且定义其顶部在虚拟地址 `UXSTACKTOP` 处，因此用户异常栈的有效字节从 `UXSTACKTOP-PGSIZE` 到 `UXSTACKTOP-1`（含）。在异常栈上运行时，用户级缺页异常处理程序可以使用 JOS 常规的系统调用来映射新页面或调整映射，从而修复最初导致缺页异常的无论什么问题。然后，用户级缺页异常处理程序通过汇编语言存根（stub）返回到在原栈上引发故障的代码。

想要支持用户级缺页异常处理的每一个用户环境都必须为自己的异常栈分配内存，这要使用在 A 部分中介绍的 `sys_page_alloc()` 系统调用。

## Invoking the User Page Fault Handler

现在你需要更改 `kern/trap.c` 中的缺页异常处理代码，按照以下方式处理来自用户模式的缺页异常。我们将故障发生时用户环境的状态称为陷入时 (trap-time) 状态。

如果没有注册缺页异常处理程序，JOS 内核会像以前一样打印一条消息并销毁用户环境。否则，内核会在异常栈上设置一个陷阱帧，它看起来像来自 `inc/trap.h` 中的 `struct UTrapframe`：

```assembly
<-- UXSTACKTOP
trap-time esp
trap-time eflags
trap-time eip
trap-time eax       start of struct PushRegs
trap-time ecx
trap-time edx
trap-time ebx
trap-time esp
trap-time ebp
trap-time esi
trap-time edi       end of struct PushRegs
tf_err (error code)
fault_va            <-- 当处理程序运行时 %esp 的位置
```

然后内核安排用户环境恢复执行，让缺页异常处理程序在该栈帧的异常栈上运行；你必须弄清楚如何使这一切发生。`fault_va` 是引起缺页异常的虚拟地址。

如果发生异常时用户环境已经在用户异常栈上运行，那么说明缺页异常处理程序本身出现了异常。在这种情况下，你应该在当前的 `tf->tf_esp` 下方开始新栈帧，而不是在 `UXSTACKTOP` 处。你应该首先压入一个空的 32 位字，然后再压入一个 `struct UTrapframe`。

要测试 `tf->tf_esp` 是否已经在用户异常栈上，检查它是否在 `UXSTACKTOP-PGSIZE` 到 `UXSTACKTOP-1` 范围之间（包含两端）。

### Exercise 9

> 实现 `kern/trap.c` 中的 `page_fault_handler` 里所需的分发缺页异常到用户模式处理程序的代码。写入异常栈时务必采取适当的预防措施。（如果用户环境在异常栈上耗尽空间会发生什么？）

当用户程序发生 Page Fault 时，CPU 会陷入内核。之前是直接把这个进程 env_destroy 干掉 。但现在，如果用户提前登记过缺页处理函数，env_pgfault_upcall 不为空，那内核就得把它救活，并且让它跳去执行那个登记好的函数。

这个用户异常栈其实是一个保护措施。考虑这个情况：如果引发缺页异常的原因是用户程序把自己的正常用户栈 USTACKTOP 写爆了，如果内核还把 Trapframe 压入那个已经溢出的用户栈，那就多重错误了。

如果用户态的缺页处理函数本身写得有问题，或者处理过程中再次触发了缺页异常，CPU 再次陷入内核，此时就发生了嵌套缺页。这个时候用户程序的栈指针 tf->tf_esp 已经在异常栈范围里了。为了不覆盖上一次的现场，我们需要顺着当前的  tf->tf_esp 继续往下压栈。根据讲义和 x86 C 语言调用约定的习惯，中间还要硬塞一个 32 位的空字，用来占位模拟返回地址。

现在我们就要在内核栈里面做一个用户态陷阱帧 struct UTrapframe，再把 CPU 接下来要恢复的现场给篡改掉。

kern/trap.c

```c
if (curenv->env_pgfault_upcall) {
    struct UTrapframe *utf;

    // 判断当前是否已处于用户异常栈 (嵌套异常)
    if (tf->tf_esp >= UXSTACKTOP - PGSIZE && tf->tf_esp < UXSTACKTOP) {
        // 在当前 esp 的基础上做下移 留出 4 字节的空字，外加 UTrapframe 的大小
        utf = (struct UTrapframe *)(tf->tf_esp - 4 - sizeof(struct UTrapframe));
    }
    else {
        // 普通异常直接放在用户异常栈的栈顶往下
        utf = (struct UTrapframe *)(UXSTACKTOP - sizeof(struct UTrapframe));
    }

    // 用户程序可能登记了 upcall，但它压根没分配异常栈的物理页
    // 如果不查就直接写，内核会在往 utf 写入数据时发生缺页，导致 panic。
    user_mem_assert(curenv, (void *)utf, sizeof(struct UTrapframe), PTE_U | PTE_W);

    // UTrapframe 构造
    utf->utf_fault_va = fault_va;
    utf->utf_err = tf->tf_err;       // 缺页的错误码
    utf->utf_regs = tf->tf_regs;
    utf->utf_eip = tf->tf_eip;
    utf->utf_eflags = tf->tf_eflags; 
    utf->utf_esp = tf->tf_esp;

    // 让 CPU 在 iret 返回用户态时，强行跳去执行 upcall 函数，并且把栈指针强行切到这个刚刚做好的用户异常栈上。
    tf->tf_eip = (uint32_t)curenv->env_pgfault_upcall;
    tf->tf_esp = (uint32_t)utf;

    // 按照篡改后的现场，恢复执行用户程序
    env_run(curenv);
}
```

> 如果用户环境在异常栈上耗尽空间会发生什么？

如果在异常栈上往下分配 utf 时空间不够，也就是超出了 UXSTACKTOP - PGSIZE 的那一页，那 user_mem_assert 检查就会失败，因为它试图访问的底层内存不存在且不在当前合法页里。user_mem_assert 会直接把这个进程给 env_destroy 掉，从而保护内核。

## User-mode Page Fault Entrypoint

接下来，你需要实现负责调用 C 语言缺页异常处理程序并在引发故障的原指令处恢复执行的汇编例程。这个汇编例程是使用 sys_env_set_pgfault_upcall() 向内核注册的处理程序。

### Exercise 10

> 实现 `lib/pfentry.S` 中的 `_pgfault_upcall` 例程。有趣的部分是返回到导致缺页异常的用户代码的原始点。你将直接返回到那里，而不经过内核。困难的部分是同时切换栈和重新加载 EIP。

理一下逻辑。现在的情况是：内核在异常栈上造好了一个 UTrapframe，然后把 eip 强行指向了 _pgfault_upcall。所以当代码执行到这里时，CPU 处于用户态，而且 %esp 正指向用户异常栈上的 UTrapframe 结构体的开头。

UTrapframe 定义如下

```c
struct UTrapframe {
    uint32_t utf_fault_va; // 偏移 0x00
    uint32_t utf_err;      // 偏移 0x04
    struct PushRegs utf_regs; // 偏移 0x08，大小 0x20 (包含 edi, esi, ebp ... 到 eax)
    uintptr_t utf_eip;     // 偏移 0x28
    uint32_t utf_eflags;   // 偏移 0x2C
    uintptr_t utf_esp;     // 偏移 0x30
}
```

我们的目标是

1. 调用 C 语言写的缺页处理函数 _pgfault_handler。

2. 恢复 utf_regs 里的所有通用寄存器。

3. 恢复 utf_eflags。

4. 把栈指针 %esp 切回到 utf_esp，也就是发生缺页时的那个正常用户栈。

5. 把指令指针 %eip 切回到 utf_eip，即引发缺页的那条汇编指令，继续执行。

主要难点在 4 5，我们在用户态，根本没有 iret，不能既改 %esp 又改 %eip。如果先改了 %esp 切回原来的栈，你就找不到保存在异常栈上的 utf_eip 了；如果先 jmp 恢复 %eip，那你原来的栈指针 %esp 就永远切不回去了。

参考注释，既然最终我们要用 ret 指令来跳回 utf_eip，而 ret 指令的动作是从当前栈顶弹出一个值赋给 %eip。那可以考虑提前把 utf_eip 的值塞到发生缺页时的那个正常用户栈 utf_esp 的栈顶位置。

JOS 的注释写的是真的详细。在 lib/pfentry.S 中

```asm
// LAB 4: Your code here.
movl 48(%esp), %ebx      // 拿到正常用户栈的栈顶指针 esp
subl $4, %ebx            // 栈顶下移 4 字节，腾出空间来放 eip
movl %ebx, 48(%esp)      // 把减去 4 之后的 esp 重新写回 UTrapframe 中

movl 40(%esp), %eax      // 拿到发生异常时的指令指针 eip
movl %eax, (%ebx)        // 把 eip 压入正常用户栈刚刚腾出的空间里

// Restore the trap-time registers.  After you do this, you
// can no longer modify any general-purpose registers.
// LAB 4: Your code here.
addl $8, %esp            // 跳过 UTrapframe 开头的 utf_fault_va (4 字节) 和 utf_err (4 字节)
popal                    // 弹出并恢复所有的通用寄存器，此时 esp 指向 utf_eip

// Restore eflags from the stack.  After you do this, you can
// no longer use arithmetic operations or anything else that
// modifies eflags.
// LAB 4: Your code here.
addl $4, %esp            // 跳过已经处理过的 utf_eip (4 字节)，此时 esp 指向 utf_eflags
popfl                    // 弹出并恢复标志寄存器 (此时 esp 指向 utf_esp)

// Switch back to the adjusted trap-time stack.
// LAB 4: Your code here.
popl %esp                // 将修改过(减去了4)的 utf_esp 弹出，直接赋给 esp 寄存器。完成切栈

// Return to re-execute the instruction that faulted.
// LAB 4: Your code here.
ret                      // 从正常用户栈顶弹出我们刚才放过去的 eip，恢复执行
```

### Exercise 11

> 完成 `lib/pgfault.c` 中的 `set_pgfault_handler()`。

这是跟着 Exercise 9 10 的，我们之前在内核写了异常的分发机制、在汇编写了切换和恢复机制，这里就是在用户空间里把它们封装起来。

set_pgfault_handler 是给用户程序调用的库函数。它的任务是：如果这是当前进程第一次注册缺页处理函数，我们需要把环境给配置好。

具体来说，首先是分配异常栈。之前说过了，异常处理不能在普通栈上搞，所以我们得用之前在 Part A 写的 sys_page_alloc，在 UXSTACKTOP - PGSIZE 的位置分配一页全新的物理内存作为异常栈。然后向内核登记入口，调用我们在 Exercise 8 写的 sys_env_set_pgfault_upcall，把汇编入口 _pgfault_upcall 告诉内核。最后保存 C 语言处理函数，把用户实际传进来的 C 语言处理函数的指针，存放到全局变量 _pgfault_handler 里。这样，当汇编代码 _pgfault_upcall 里执行 call *%eax 时，就能准确地调到用户的业务逻辑了。

判断是不是第一次调用也很简单，直接看全局变量 _pgfault_handler 是不是 0 就行。

```c
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		// 为当前进程分配一页物理内存作为用户异常栈，地址是 UXSTACKTOP 向下一页，权限需要可读可写、用户态可访问
		if ((r = sys_page_alloc(0, (void *)(UXSTACKTOP - PGSIZE), PTE_U | PTE_P | PTE_W)) < 0) {
			panic("set_pgfault_handler: sys_page_alloc failed %e", r);
		}

		// 向内核登记汇编的缺页异常入口点。0 代表当前环境，_pgfault_upcall 就是我们在 pfentry.S 里写的那个函数
		if ((r = sys_env_set_pgfault_upcall(0, _pgfault_upcall)) < 0) {
			panic("set_pgfault_handler: sys_env_set_pgfault_upcall failed %e", r);
		}
		// panic("set_pgfault_handler not implemented");
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
}
```

log 如下

```shell
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make run-faultread
make[1]: Entering directory '/home/iplayforsg/Desktop/MIT_6.828'
+ cc kern/init.c
+ cc[USER] lib/pgfault.c
+ ar obj/lib/libjos.a
+ ld obj/user/hello
+ ld obj/user/buggyhello
+ ld obj/user/buggyhello2
+ ld obj/user/evilhello
+ ld obj/user/testbss
+ ld obj/user/divzero
+ ld obj/user/breakpoint
+ ld obj/user/softint
+ ld obj/user/badsegment
+ ld obj/user/faultread
+ ld obj/user/faultreadkernel
+ ld obj/user/faultwrite
+ ld obj/user/faultwritekernel
+ ld obj/user/idle
+ ld obj/user/yield
+ ld obj/user/dumbfork
+ ld obj/user/stresssched
+ ld obj/user/faultdie
+ ld obj/user/faultregs
+ ld obj/user/faultalloc
+ ld obj/user/faultallocbad
+ ld obj/user/faultnostack
+ ld obj/user/faultbadhandler
+ ld obj/user/faultevilhandler
+ ld obj/user/forktree
+ ld obj/user/sendpage
+ ld obj/user/spin
+ ld obj/user/fairness
+ ld obj/user/pingpong
+ ld obj/user/pingpongs
+ ld obj/user/primes
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/iplayforsg/Desktop/MIT_6.828'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
CPU Supports PSE.
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] user fault va 00000000 ip 00800039
TRAP frame at 0xf02b1000 from CPU 0
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0xeec00000
  es   0x----0023
  ds   0x----0023
  trap 0x0000000e Page Fault
  cr2  0x00000000
  err  0x00000004 [user, read, not-present]
  eip  0x00800039
  cs   0x----001b
  flag 0x00000086
  esp  0xeebfdfc0
  ss   0x----0023
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make run-faultdie
make[1]: Entering directory '/home/iplayforsg/Desktop/MIT_6.828'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/iplayforsg/Desktop/MIT_6.828'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
CPU Supports PSE.
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
i faulted at va deadbeef, err 6
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make run-faultalloc
make[1]: Entering directory '/home/iplayforsg/Desktop/MIT_6.828'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/iplayforsg/Desktop/MIT_6.828'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
CPU Supports PSE.
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
fault deadbeef
this string was faulted in at deadbeef
fault cafebffe
fault cafec000
this string was faulted in at cafebffe
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make run-faultallocbad
make[1]: Entering directory '/home/iplayforsg/Desktop/MIT_6.828'
+ cc kern/init.c
+ ld obj/kern/kernel
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/iplayforsg/Desktop/MIT_6.828'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log -smp 1 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
Physical memory: 131072K available, base = 640K, extended = 130432K
check_page_free_list() succeeded!
check_page_alloc() succeeded!
check_page() succeeded!
CPU Supports PSE.
check_kern_pgdir() succeeded!
check_page_free_list() succeeded!
check_page_installed_pgdir() succeeded!
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] user_mem_check assertion failure for va deadbeef
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
iplayforsg@ubuntu:~/Desktop/MIT_6.828$ 
```

那么我们的缺页异常处理没问题。

## Implementing Copy-on-Write Fork

你现在已经拥有了完全在用户空间中实现写时复制 `fork()` 的内核设施。

我们在 `lib/fork.c` 中为你的 `fork()` 提供了骨架。与 `dumbfork()` 类似，`fork()` 应该创建一个新环境，然后扫描父环境的整个地址空间，并在子环境中设置相应的页面映射。关键的区别在于，虽然 `dumbfork()` 复制的是页面，但 `fork()` 最初将只复制页面映射。`fork()` 只有当其中一个环境试图对其写入时才会复制该页面。

`fork()` 的基本控制流程如下：

1. 父进程使用你在上面实现的 `set_pgfault_handler()` 函数，将 `pgfault()` 安装为 C 级别的缺页异常处理程序。
2. 父进程调用 `sys_exofork()` 创建一个子环境。
3. 对于在其 UTOP 以下地址空间中每个可写或写时复制的页面，父进程调用 `duppage`，它应将该页面以写时复制方式映射到子进程的地址空间中，然后重新在自身的地址空间中以写时复制方式映射该页面。[ 注：这里的顺序（即在父进程中标记页面为 COW 之前，先在子进程中标记）实际上很重要！你能看出为什么吗？试着想出一个具体的情境，其中颠倒顺序可能会引起麻烦。 ] `duppage` 设置两个 PTE 使页面变为不可写，并在“avail”字段中包含 `PTE_COW`，以区分写时复制页面和真正的只读页面。 然而，异常栈不会以这种方式被重新映射。相反，你需要在子进程中为异常栈分配一个新页。由于将进行实际的复制工作的是缺页异常处理程序，并且该处理程序运行在异常栈上，因此异常栈不能被设置为写时复制：否则谁去复制它呢？
4. `fork()` 还需要处理那些存在但既不可写也不是写时复制的页面。
5. 父进程将子进程的用户缺页异常入口点设置得与自己一样。
6. 子进程现在准备好运行了，因此父进程将其标记为可运行。

每当其中一个环境对其尚未写入的写时复制页面执行写入操作时，就会产生一次缺页异常。这是用户缺页异常处理程序的控制流：

1. 内核将缺页异常传播给 `_pgfault_upcall`，后者调用 `fork()` 的 `pgfault()` 处理程序。
2. `pgfault()` 检查异常是否属于写入操作（在错误代码中检查 `FEC_WR`）并且页面的 PTE 被标记为了 `PTE_COW`。如果不是，就触发 panic。
3. `pgfault()` 分配一个映射在临时位置的新页，并将故障页的内容复制进其中。然后异常处理程序用读/写权限在合适的地址映射这个新页面，从而取代旧的只读映射。

用户级的 `lib/fork.c` 代码必须查询环境的页表，以执行上述的几项操作（例如确认某个页面的 PTE 是否被标记为了 `PTE_COW`）。内核为了这个特定的目的，将环境的页表映射在了 `UVPT`。它使用了一个聪明的映射技巧，这使用户代码查找 PTE 变得非常容易。`lib/entry.S` 设置了 `uvpt` 和 `uvpd`，使得你可以轻松地在 `lib/fork.c` 中查找页表信息。

### Exercise 12

> 在 `lib/fork.c` 中实现 `fork`、`duppage` 和 `pgfault`。
>
> 用 `forktree` 程序测试你的代码。它应该产生以下信息，穿插有 'new env', 'free env', 以及 'exiting gracefully' 的信息。信息出现的顺序可能会不同，环境 ID 也可能不同。
>
> ```
> 1000: I am ''
> 1001: I am '0'
> 2000: I am '00'
> 2001: I am '000'
> 1002: I am '1'
> 3000: I am '11'
> 3001: I am '10'
> 4000: I am '100'
> 1003: I am '01'
> 5000: I am '010'
> 4001: I am '011'
> 2002: I am '110'
> 1004: I am '001'
> 1005: I am '111'
> 1006: I am '101'
> ```

这里其实就是在写 COW 了。fork() 作为主控，创建一个空壳子进程，遍历父进程的页表，把有效的页以只读 + COW 标志映射给子进程，同时把自己也变成只读 + COW 标志。duppage() 则负责具体的映射，当父子双方有任何一方试图写入被改成只读的页时，必然触发 Page Fault。此时触发 pgfault() ，发现是因为 PTE_COW 导致的缺页，于是分配一页新物理内存，把数据复制过去，再把页权限改回可写。

那么，首先实现 pgfault()。当缺页异常发生并切到用户态处理时，会调用这个函数。 我们首先要鉴别这到底是不是写时复制触发的缺页。那么，必须同时满足两个条件：缺页是因为写操作引起的（FEC_WR），且这个页的页表项上标有我们自定义的 PTE_COW。 如果确认是 COW 缺页，我们就分配一个新页，把数据复制进去。为了避免直接覆盖自己原来的只读页导致死循环缺页，我们得先把它映射到一个临时虚拟地址 PFTEMP，复制完数据后，再把它映射回发生缺页的那个地址。

```c
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
```

然后是 duppage()，负责把父进程的某一页映射给子进程。

讲义里写了这样一句话

> 注：这里的顺序（即在父进程中标记页面为 COW 之前，先在子进程中标记）实际上很重要！你能看出为什么吗？试着想出一个具体的情境，其中颠倒顺序可能会引起麻烦。

假设我们颠倒顺序，先把父进程的页设为 PTE_COW，再去设子进程。那么在这两步操作之间，很可能会发生时钟中断导致进程切换，或者父进程立马去写这页内存。如果父进程写了这页内存，就会触发 Page Fault，进入刚才写的 pgfault() 分配一页新的、可写的物理页给自己。等父进程处理完缺页，回来继续往下执行 duppage 映射子进程时，它会把这个可写的全新物理页映射给子进程，并且标记为 PTE_COW。

结果就是：父子进程共享了同一个物理页，但父进程有写权限，子进程是 COW。以后父进程再写这页时，根本不会触发复制，它会直接篡改子进程的数据，这直接破坏了进程间的隔离性。所以，必须先把子进程设为 COW，再设父进程，才能保证安全。

```c
pte_t pte = uvpt[pn];
	
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
```

最后是 fork()，我们需要遍历当前进程的页表。在 JOS 里，uvpd 数组可以直接读取页目录，uvpt 数组可以直接读取页表。我们只需遍历 UTOP 以下的所有虚拟地址空间即可。

```c
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
		// 这里是子进程的代码路径，父进程会把环境全布置好
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

	// 为子进程分配专属的异常栈
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
```

make run-forktree，可以发现 I am 全部通过。此时 make grade，Part A  + Part B 应该全部是满分

---

至此 B 部分结束。请确保在运行 `make grade` 时，通过所有针对 B 部分的测试。像往常一样，你可以使用 `make handin` 来提交。

# Part C: Preemptive Multitasking and Inter-Process communication (IPC)

在 lab 4 的最后一部分，你将修改内核以抢占那些不合作的环境，并允许环境彼此显式地传递消息。

## Clock Interrupts and Preemption

运行 `user/spin` 测试程序。这个测试程序 fork 出一个子环境，一旦子环境取得 CPU 的控制权，它便在一个紧密的循环里永久地自旋。无论是父环境还是内核都永远拿不回 CPU。就保护系统免受用户模式环境内的 bug 或者恶意代码的影响而言，这显然并非理想情形，因为任何一个用户模式环境都可以单单凭借陷入无限循环且永不将 CPU 交还，就将整个系统带入停滞状态。为了允许内核抢占正在运行的环境，强制性地将 CPU 控制权从其手中夺回，我们必须扩展 JOS 内核来支持来自时钟硬件的外部硬件中断。

## Interrupt discipline

外部中断（即设备中断）被称为 IRQ。共有 16 个可能的 IRQ，编号为 0 至 15。从 IRQ 编号到 IDT 入口的映射并未固定。`picirq.c` 中的 `pic_init` 将 0-15 号 IRQ 映射至从 `IRQ_OFFSET` 到 `IRQ_OFFSET+15` 的 IDT 入口处。

在 `inc/trap.h` 中，`IRQ_OFFSET` 被定义为十进制数 32。因此，IDT 入口 32-47 便对应于 0-15 号 IRQ。例如，时钟中断是 IRQ 0。因此，IDT[IRQ_OFFSET+0]（即 IDT[32]）容纳了内核中时钟中断处理程序的地址。这般选取 `IRQ_OFFSET` 使得设备中断不会与处理器异常发生重叠，重叠显然能够导致混乱。（事实上，在运行 MS-DOS 的早期个人电脑上，`IRQ_OFFSET` 实际上正是零，这就真的在处理硬件中断与处理处理器异常之间引发了巨大的混乱！）

在 JOS 中，相较 xv6 Unix 而言，我们进行了一项关键的简化。在处于内核中时，外部设备中断被永远禁用（而在用户空间则同 xv6 一样予以启用）。外部中断受到 `%eflags` 寄存器的 `FL_IF` 标志位的控制（详见 `inc/mmu.h`）。当该位被设置时，外部中断就启用了。尽管可以通过好几种办法去修改该标志位，但鉴于我们做出的简化，我们将完全凭借在进出用户模式之际保存与恢复 `%eflags` 寄存器的流程来处理它。

你必须确保用户环境运行时设置了 `FL_IF` 标志位，好让中断到来之时，它能被透传进处理器并交由你的中断代码去处理。否则，中断就会被掩蔽 (masked)，或者说被忽视，直至重新启用中断。我们已经在引导加载程序的最初那条指令中掩蔽了中断，迄今我们还从没来得及去重新启用它们。

### Exercise 13

> 修改 `kern/trapentry.S` 和 `kern/trap.c`，初始化 IDT 中的相应条目，并提供 IRQ 0 到 15 的处理程序。然后修改 `kern/env.c` 中的 `env_alloc()` 代码，确保用户环境始终在启用中断的情况下运行。 另外，取消 `sched_halt()` 中 `sti` 指令的注释，以便空闲的 CPU 取消屏蔽中断。

在 x86 架构里，硬件设备触发的中断 IRQ 的编号是 0 到 15。但问题是，CPU 自己内部的异常占用了 IDT 表的前 32 个位置。如果让时钟中断直接占 0 号位置，那就和除零异常冲突了。 所以，JOS 在初始化中断控制器时，把 IRQ 0~15 整体平移了 32 个位置，映射到了 IDT 的 IRQ_OFFSET 到 IRQ_OFFSET + 15，也就是 32 到 47。

所有的硬件外部中断都是不产生错误码的。所以我们在 trapentry.S 里必须用 TRAPHANDLER_NOEC。除此之外，用户程序绝对不能通过 int 指令去主动触发硬件中断，所以 IDT 表里这些门描述符的特权级必须设为 0。

首先，kern/trapentry.S 中声明 16 个 IRQ 的汇编入口

```asm
TRAPHANDLER_NOEC(irq_0, IRQ_OFFSET + 0);
TRAPHANDLER_NOEC(irq_1, IRQ_OFFSET + 1);
TRAPHANDLER_NOEC(irq_2, IRQ_OFFSET + 2);
TRAPHANDLER_NOEC(irq_3, IRQ_OFFSET + 3);
TRAPHANDLER_NOEC(irq_4, IRQ_OFFSET + 4);
TRAPHANDLER_NOEC(irq_5, IRQ_OFFSET + 5);
TRAPHANDLER_NOEC(irq_6, IRQ_OFFSET + 6);
TRAPHANDLER_NOEC(irq_7, IRQ_OFFSET + 7);
TRAPHANDLER_NOEC(irq_8, IRQ_OFFSET + 8);
TRAPHANDLER_NOEC(irq_9, IRQ_OFFSET + 9);
TRAPHANDLER_NOEC(irq_10, IRQ_OFFSET + 10);
TRAPHANDLER_NOEC(irq_11, IRQ_OFFSET + 11);
TRAPHANDLER_NOEC(irq_12, IRQ_OFFSET + 12);
TRAPHANDLER_NOEC(irq_13, IRQ_OFFSET + 13);
TRAPHANDLER_NOEC(irq_14, IRQ_OFFSET + 14);
TRAPHANDLER_NOEC(irq_15, IRQ_OFFSET + 15);
```

然后 kern/trap.c

```c
void irq_0();
void irq_1();
void irq_2();
void irq_3();
void irq_4();
void irq_5();
void irq_6();
void irq_7();
void irq_8();
void irq_9();
void irq_10();
void irq_11();
void irq_12();
void irq_13();
void irq_14();
void irq_15();

SETGATE(idt[IRQ_OFFSET + 0], 0, GD_KT, irq_0, 0);
SETGATE(idt[IRQ_OFFSET + 1], 0, GD_KT, irq_1, 0);
SETGATE(idt[IRQ_OFFSET + 2], 0, GD_KT, irq_2, 0);
SETGATE(idt[IRQ_OFFSET + 3], 0, GD_KT, irq_3, 0);
SETGATE(idt[IRQ_OFFSET + 4], 0, GD_KT, irq_4, 0);
SETGATE(idt[IRQ_OFFSET + 5], 0, GD_KT, irq_5, 0);
SETGATE(idt[IRQ_OFFSET + 6], 0, GD_KT, irq_6, 0);
SETGATE(idt[IRQ_OFFSET + 7], 0, GD_KT, irq_7, 0);
SETGATE(idt[IRQ_OFFSET + 8], 0, GD_KT, irq_8, 0);
SETGATE(idt[IRQ_OFFSET + 9], 0, GD_KT, irq_9, 0);
SETGATE(idt[IRQ_OFFSET + 10], 0, GD_KT, irq_10, 0);
SETGATE(idt[IRQ_OFFSET + 11], 0, GD_KT, irq_11, 0);
SETGATE(idt[IRQ_OFFSET + 12], 0, GD_KT, irq_12, 0);
SETGATE(idt[IRQ_OFFSET + 13], 0, GD_KT, irq_13, 0);
SETGATE(idt[IRQ_OFFSET + 14], 0, GD_KT, irq_14, 0);
SETGATE(idt[IRQ_OFFSET + 15], 0, GD_KT, irq_15, 0);
```

kern/env.c 的 env_alloc() 也要改， x86 的 EFLAGS 寄存器里有一个位叫 FL_IF（Interrupt Flag）。如果这个位是 0，CPU 就会在硬件级别屏蔽所有外部中断。我们在把进程送上 CPU 前得打开这个位。

```c
// Enable interrupts while in user mode.
// LAB 4: Your code here.
e->env_tf.tf_eflags |= FL_IF;
```

最后把 kern/sched.c 中的 asm volatile 的 sti 注释删掉

## Handling Clock Interrupts

在 `user/spin` 程序中，在子环境首度运行后，它仅仅就在一次循环里自旋了，内核便再也不曾夺回控制权。我们需要对硬件予以编程用以定期地生成时钟中断，那会把控制权强制夺回给内核，在那里我们可以将控制权切换去另一个不同的用户环境上。

由我们为你编写好的对 `lapic_init` 与 `pic_init` 的调用（来自 `init.c` 中的 `i386_init`），建立好了时钟与中断控制器去生成中断。眼下你须编写去处理那些中断的代码了。

### Exercise 14

> 修改内核的 `trap_dispatch()` 函数，使其在每次发生时钟中断时调用 `sched_yield()` 来查找并运行不同的环境。

我们要在 trap_dispatch 里把时钟中断IRQ_OFFSET + IRQ_CLOCK 拎出来，一旦收到这个中断，就意味着当前进程的时间片用完了，立刻强制调用 sched_yield() 把 CPU 强行塞给下一个进程。

注意 lapic_eoi()。在现代多核架构里，中断是由高级可编程中断控制器（APIC）分发的。如果你收到了它的中断信号，但迟迟不给它回执（End Of Interrupt, EOI），APIC 就会认为你还没处理完，从此以后再也不会给你发送时钟信号。 所以，在调用 sched_yield() 切走之前，必须先调用 lapic_eoi()，告诉硬件：收到信号，请继续发送。

在 kern/trap.c 中修改 trap_dispatch()

```c
if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
    // 告诉 Local APIC 中断已收到
    lapic_eoi();
    // 2剥夺当前进程控制权，重新调度
    sched_yield();
    return;
}
```

此时 make run-spin

```shell
[00000000] new env 00001000
I am the parent.  Forking the child...
[00001000] new env 00001001
I am the parent.  Running the child...
I am the child.  Spinning...
I am the parent.  Killing the child...
[00001000] destroying 00001001
[00001000] free env 00001001
[00001000] exiting gracefully
[00001000] free env 00001000
No runnable environments in the system!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated

```

可以发现 parent kill 掉了子进程，没问题。

## Inter-Process communication (IPC)

（从技术层面讲在 JOS 之中这叫做“环境间通信 (inter-environment communication)”或者是“IEC”，奈何其余人都将其叫作 IPC，因此我们将沿用这项标准叫法。）

我们在操作系统的各项隔离性方面着眼已久，即其为每套程序提供犹如独占整台机器一样的错觉的种种手段。操作系统的另一类重要服务便是在各程序试图交流时，容许各程序互通声气。让各大程序与其他程序交融互动的能耐是可以相当强大的。Unix pipe（管道）模型就是其中一项典范。

有着许多可供作进程间通信的模型。时至今日仍关于何种模型堪称最佳而争论纷纷。我们不打算陷进那个论调当中去。反而，我们会构建出一套简单的 IPC 机制并尝试着去运用它。

## IPC in JOS

你会再实现少许的额外 JOS 内核系统调用，它们将共同搭建起一个简易的进程间通信机制。你须实现两个系统调用，即 `sys_ipc_recv` 以及 `sys_ipc_try_send`。然后你会去实现两套封装成库形式的方法 `ipc_recv` 与 `ipc_send`。

用户环境能够采用 JOS 的 IPC 机制来发予彼此的“消息 (messages)”之中包含两项成分：一项单独的 32 位数值，以及可选的单独一页页面映射。允许众多环境于消息之中传递各个页面映射会提供了一种行之有效的方式，它能转移更多塞不进单独的 32 位整型里的数据，也允许各大环境轻易地建立起共享内存这类安排。

## Sending and Receiving Messages

为去收取一则消息，某环境调用了 `sys_ipc_recv`。这一项系统调用能够将当下的环境撤出调度且绝不会去再度运行，直到有一则消息已被收取为止。当一个环境等候着获取一则消息之际，任何别的环境都能够对它派送出消息——它不局限于特定的哪一个环境，更不仅仅是局限于那些在接收的环境那里构成了父子关系安排的那些环境。换一种说法，这套你在 A 部分中构筑出来的权限验证在这里的 IPC 里将概不适用，因为 IPC 的多项系统调用统统经了精心设计以至成为“安全的 (safe)”：某个环境断不会单单发发消息就害得其它的环境出差错（除非目标的环境本身也是千疮百孔的）。

去试着发出一个数值，某一环境要使用被发出的那一数值协同收取方的环境 ID 一道呼叫 `sys_ipc_try_send`。假若所指名的这环境其实处在接收当中（其调用了 `sys_ipc_recv`，却也没能拿到什么数值），则该发出操作将可送达那一消息，并且会返回 0。要不然，该发出操作会返还 `-E_IPC_NOT_RECV`，指示这目标环境此刻并未在预备着收取某个值。

一项目处在用户空间中的库函数 `ipc_recv` 将包办对于 `sys_ipc_recv` 的那番呼叫操作，转而在此当前环境名下的 `struct Env` 当中查阅那已然接收到的多重数值里头藏着的情报。

类似地，一项库函数 `ipc_send` 也将包管于循环里反反复复地调取 `sys_ipc_try_send` 直至那番发出操作功德圆满。

## Transferring Pages

一旦有某个环境伴随有一个合法合规的 `dstva` 参量（低于 `UTOP`）一道调用了 `sys_ipc_recv` ，此环境就在言明自身乐于揽收一份页表映射。要是发信一方递送来了一张页面，这么这张页面当被映射在这收信方地址空间中的 `dstva` 那边。设若收信方先前业已有一张页面被映射到了 `dstva` 之上，那么以前那页面即遭撤销映射。

一旦某一环境随同一处合法合规的 `srcva`（在 `UTOP` 之底）共同呼出 `sys_ipc_try_send` 的时候，这就是意味着该发信者企望用此项权限许可 `perm`，连同眼下被映射在这 `srcva` 里的那一页发送入这个收信者手里。待一单顺顺利利的 IPC 过后，对于那张呆在 `srcva` 的页面而言这发信方将其初始的这套映射保留在了它的地址空间之内，只不过那发信方依然在收信方一开始就特定说妥的那一 `dstva` 这，在该接收方的地址空间之内取得了对于这一同一张物理页的一套映射结果。以此为结果这张页面则被转化为了发送跟接收两头都予以分享的状态。

假设发收其中某一方丝毫没曾明示某页能够被转交，这么这页也便无从转交。但凡一切 IPC 过后内核将把在这接收方名下 `Env` 数据结构之内那个名叫 `env_ipc_perm` 的新设栏位里设置为这遭到接纳的页面的各种权限；或者倘使没有拿稳什么页面那便将设置为 0。

## Implementing IPC

### Exercise 15

> 在 `kern/syscall.c` 中实现 `sys_ipc_recv` 和 `sys_ipc_try_send` 。实现前请阅读这两个函数的注释，因为它们必须协同工作。在这些例程中调用 `envid2env` 时，应将 `checkperm` 标志设置为 0，这意味着允许任何环境向任何其他环境发送 IPC 消息，内核除了验证目标 envid 是否有效之外，不会进行任何特殊的权限检查。
>
> 然后实现 `lib/ipc.c` 中的 `ipc_recv` 和 `ipc_send` 函数。
>
> 使用 `user/pingpong` 和 `user/primes` 函数来测试你的进程间通信 (IPC) 机制。 `user/primes` 函数会为每个素数生成一个新的环境，直到 JOS 耗尽所有环境为止。你可能会对阅读 `user/primes.c` 感兴趣，它展示了幕后所有的进程分叉和 IPC 操作。

JOS 的 IPC 很精简，它只传递一个 32 位的值和一个物理页的映射权。

接收方 sys_ipc_recv 主动交出 CPU，挂起自己，向全系统广播“我准备好接收了”。发送方 sys_ipc_try_send 尝试塞东西给目标进程。如果目标进程恰好正在等（env_ipc_recving == 1），就把数据和页映射塞进它的 struct Env 结构体里，然后把它唤醒。如果目标不在等，发送方就不阻塞等待，而是直接返回一个失败码 -E_IPC_NOT_RECV，由用户态去决定要不要等会儿再试。

这个实现分为用户态和内核态两步。

首先内核态，在 kern/syscall.c 中，我们先实现接收者 sys_ipc_recv。这个调用的任务就是让自己开始等待。注意，即使操作成功，这个函数也不应该直接 return 0 返回用户态，而是要调用 sched_yield() 把 CPU 交给别人。

```c
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
```

然后是实现发送方 sys_ipc_try_send。这个有点麻烦，有很多合法性校验。

另外，目标进程是被 sched_yield 挂起的。发送方在唤醒它之前，必须手动把目标进程的 %eax 寄存器（保存在 env_tf.tf_regs.reg_eax 里）改成 0。这样目标进程将来醒来并恢复上下文时，就会以为是 sys_ipc_recv 系统调用成功返回了 0。

```c
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
```

然后注册系统调用

```c
case SYS_ipc_try_send:
	return sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);

case SYS_ipc_recv:
	return sys_ipc_recv((void *)a1);
```

接下来是用户态，在 lib/ipc.c 中用 ipc_recv 和 ipc_send 进行封装。

首先 ipc_recv，这个函数只是简单地调用 sys_ipc_recv，醒来后从全局变量 thisenv 里把其它进程塞进来的数据取出来返回。

```c
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	// LAB 4: Your code here.
	int r;
	
	// 如果用户没传页地址，就传个大于 UTOP 的假地址给内核
	void *dstva = pg ? pg : (void *)UTOP;

	if ((r = sys_ipc_recv(dstva)) < 0) {
		// 如果参数传错了
		if (from_env_store) *from_env_store = 0;
		if (perm_store) *perm_store = 0;
		return r;
	}

	// 此时进程已经被发信方唤醒，thisenv 结构体里已经有数据了
	if (from_env_store) *from_env_store = thisenv->env_ipc_from;
	if (perm_store) *perm_store = thisenv->env_ipc_perm;

	return thisenv->env_ipc_value;

	// panic("ipc_recv not implemented");
	// return 0;
}
```

然后 ipc_send，这里顺便解答了讲义里的一个 Challenge 问题：“为什么 ipc_send 需要循环？” 

因为目标进程可能正在忙别的，还没来得及调用 ipc_recv。所以我们得不断尝试，如果是 -E_IPC_NOT_RECV，说明目标没准备好，我们就调用 sys_yield() 放弃这次时间片，等下一轮再试，实际上就是在做轮询重试。

```c
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	// LAB 4: Your code here.
	int r;
	void *srcva = pg ? pg : (void *)UTOP;

	while (1) {
		r = sys_ipc_try_send(to_env, val, srcva, perm);
		
		if (r == 0) {
			// 发送成功
			return;
		}
		else if (r == -E_IPC_NOT_RECV) {
			// 目标还没准备好，让出 CPU，等会再试
			sys_yield();
		}
		else {
			// 其他乱七八糟的致命错误
			panic("ipc_send: failed %e", r);
		}
	}

	// panic("ipc_send not implemented");
}
```



现在 make grade

![[MIT 6.828 Lab 4] 3](img\[MIT 6.828 Lab 4] 3.png)

done
