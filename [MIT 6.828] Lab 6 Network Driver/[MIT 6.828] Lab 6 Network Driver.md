One last。后面有空把 Lab 4 5 6 的 Challenge 补了，做完了 Exercise 后再写 Challenge 应该就没那么多冲突报错了

吧大概

# Lab 6: Network Driver

## QEMU's virtual network

我们将使用 QEMU 的用户模式网络栈，因为它不需要管理员权限即可运行。QEMU 的文档中有更多关于 user-net 的说明。我们已经更新了 makefile，以启用 QEMU 的用户模式网络栈和虚拟 E1000 网卡。

默认情况下，QEMU 会提供一个运行在 IP `10.0.2.2` 上的虚拟路由器，并将 IP 地址 `10.0.2.15` 分配给 JOS。为了保持简单，我们在 `net/ns.h` 的网络服务器中硬编码了这些默认值。

虽然 QEMU 的虚拟网络允许 JOS 任意向外连接到 Internet，但 JOS 的 `10.0.2.15` 地址在 QEMU 内部运行的虚拟网络之外没有意义（也就是说，QEMU 充当 NAT），因此我们不能直接连接到运行在 JOS 内部的服务器，即使是从运行 QEMU 的宿主机上也不行。为了解决这个问题，我们配置 QEMU 在宿主机的某个端口上运行一个服务器，它会简单地连接到 JOS 中的某个端口，并在你的真实宿主机和虚拟网络之间来回转发数据。

你将在端口 7（echo）和 80（http）上运行 JOS 服务器。makefile 会基于你的用户 ID 为这些端口生成转发端口。要找出 QEMU 在你的开发主机上转发到了哪些端口，请运行 `make which-ports`。为方便起见，makefile 还提供了 `make nc-7` 和 `make nc-80`，允许你直接在终端中与运行在这些端口上的服务器交互。（这些目标只会连接到一个正在运行的 QEMU 实例；你必须单独启动 QEMU 本身。）

## Packet Inspection

makefile 还配置了 QEMU 的网络栈，将所有传入和传出的数据包记录到你实验目录中的 `qemu.pcap`。

要获得捕获数据包的十六进制/ASCII 转储，可以像这样使用 `tcpdump`：

```text
tcpdump -XXnr qemu.pcap
```

或者，你可以使用 Wireshark 图形化地检查 pcap 文件。Wireshark 还知道如何解码和检查数百种网络协议。

## Debugging the E1000

我们很幸运使用的是仿真硬件。由于 E1000 运行在软件中，被仿真的 E1000 能够以用户可读的格式向我们报告其内部状态以及遇到的任何问题。通常，对于在裸机上编写驱动程序的开发者来说，这种便利是不存在的。

E1000 可以产生大量调试输出，因此你必须启用特定的日志通道。你可能会发现以下一些通道很有用：

| Flag        | Meaning                          |
| ----------- | -------------------------------- |
| `tx`        | 记录数据包发送操作               |
| `txerr`     | 记录发送环错误                   |
| `rx`        | 记录对 RCTL 的更改               |
| `rxfilter`  | 记录对传入数据包的过滤           |
| `rxerr`     | 记录接收环错误                   |
| `unknown`   | 记录对未知寄存器的读写           |
| `eeprom`    | 记录从 EEPROM 的读取             |
| `interrupt` | 记录中断以及对中断寄存器的更改。 |

例如，要启用 `tx` 和 `txerr` 日志，可以使用 `make E1000_DEBUG=tx,txerr ...`。

注意：`E1000_DEBUG` 标志只在 6.828 版本的 QEMU 中有效。

你可以进一步利用软件仿真硬件来调试。如果你遇到卡住的情况，不明白为什么 E1000 没有按你预期的方式响应，你可以查看 QEMU 中的 E1000 实现：`hw/net/e1000.c`。

## The Network Server

从零开始编写网络协议栈是一项艰巨的工作。因此，我们将使用 lwIP，这是一个开源的轻量级 TCP/IP 协议套件，其中包含了网络协议栈等许多内容。你可以在这里找到更多关于 lwIP 的信息。在本作业中，就我们而言，lwIP 是一个黑盒：它实现了 BSD socket 接口，并有一个数据包输入端口和一个数据包输出端口。

网络服务器实际上由四个环境组合而成：

- core network server environment（包括 socket 调用分发器和 lwIP）
- input environment
- output environment
- timer environment

下图展示了这些不同环境及其关系。该图展示了整个系统，包括稍后会介绍的设备驱动。在本实验中，你将实现图中以绿色高亮的部分。

![[MIT 6.828 Lab 6] 1](img\[MIT 6.828 Lab 6] 1.png)

## The Core Network Server Environment

核心网络服务器环境由 socket 调用分发器和 lwIP 本身组成。socket 调用分发器的工作方式与文件服务器完全相同。用户环境使用 stub（位于 `lib/nsipc.c`）向核心网络环境发送 IPC 消息。如果你查看 `lib/nsipc.c`，会看到我们查找核心网络服务器的方式与查找文件服务器的方式相同：`i386_init` 创建了类型为 `NS_TYPE_NS` 的 NS 环境，因此我们扫描 `envs`，寻找这个特殊的环境类型。对于每个用户环境 IPC，网络服务器中的分发器都会代表该用户调用 lwIP 提供的相应 BSD socket 接口函数。

普通用户环境不会直接使用 `nsipc_*` 调用。相反，它们使用 `lib/sockets.c` 中的函数，这些函数提供了基于文件描述符的 sockets API。因此，用户环境通过文件描述符引用 socket，就像它们引用磁盘文件一样。许多操作（`connect`、`accept` 等）是 socket 特有的，但 `read`、`write` 和 `close` 会通过 `lib/fd.c` 中普通的文件描述符设备分发代码。类似于文件服务器为所有打开的文件维护内部唯一 ID，lwIP 也会为所有打开的 socket 生成唯一 ID。在文件服务器和网络服务器中，我们都使用存储在 `struct Fd` 中的信息，将每个环境的文件描述符映射到这些唯一 ID 空间。

尽管文件服务器和网络服务器的 IPC 分发器看起来行为相同，但有一个关键区别。像 `accept` 和 `recv` 这样的 BSD socket 调用可能会无限期阻塞。如果分发器让 lwIP 执行这些阻塞调用之一，分发器本身也会阻塞，那么整个系统中同一时间只能有一个未完成的网络调用。这是不可接受的，因此网络服务器使用用户级线程来避免阻塞整个服务器环境。对于每一条传入的 IPC 消息，分发器都会创建一个线程，并在新创建的线程中处理该请求。如果该线程阻塞，那么只有这个线程会睡眠，而其他线程可以继续运行。

除了核心网络环境外，还有三个辅助环境。核心网络环境的分发器除了接受来自用户应用程序的消息外，也接受来自 input 和 timer 环境的消息。

## The Output Environment

在为用户环境的 socket 调用提供服务时，lwIP 会生成要由网卡发送的数据包。lwIP 会使用 `NSREQ_OUTPUT` IPC 消息，将每个需要发送的数据包发送给 output 辅助环境，并通过 IPC 消息的 page 参数附带该数据包。output 环境负责接受这些消息，并通过你很快将创建的系统调用接口把数据包转发给设备驱动。

## The Input Environment

网卡接收到的数据包需要注入到 lwIP 中。对于设备驱动接收到的每个数据包，input 环境会把该数据包从内核空间取出（使用你将实现的内核系统调用），并使用 `NSREQ_INPUT` IPC 消息将数据包发送给核心服务器环境。

数据包输入功能与核心网络环境分离，是因为 JOS 很难同时接受 IPC 消息并轮询或等待来自设备驱动的数据包。JOS 中没有 `select` 系统调用，无法让环境监视多个输入源以识别哪一个输入已经准备好处理。

如果你查看 `net/input.c` 和 `net/output.c`，会发现二者都需要实现。这主要是因为其实现依赖于你的系统调用接口。你将在实现驱动和系统调用接口之后，为这两个辅助环境编写代码。

## The Timer Environment

timer 环境会定期向核心网络服务器发送类型为 `NSREQ_TIMER` 的消息，通知它某个定时器已经过期。来自这个线程的 timer 消息被 lwIP 用来实现各种网络超时。

## Part A: Initialization and transmitting packets

你的内核还没有时间概念，因此我们需要添加它。目前硬件每 10ms 会产生一次时钟中断。每次时钟中断时，我们可以递增一个变量，表示时间已经前进了 10ms。这在 `kern/time.c` 中已经实现，但还没有完全集成到你的内核中。

### Exercise 1

> 在 `kern/trap.c` 中，为每次时钟中断添加对 `time_tick` 的调用。实现 `sys_time_msec`，并在 `kern/syscall.c` 中把它加入 `syscall`，这样用户空间就可以访问时间。
>
> 使用 `make INIT_CFLAGS=-DTEST_NO_NS run-testtime` 来测试你的时间代码。你应该会看到环境以 1 秒间隔从 5 开始倒计时。`-DTEST_NO_NS` 会禁止启动网络服务器环境，因为在实验的这个阶段它还会 panic。

根据讲义，我们需要在时钟中断的 dispatcher 里加一行代码推进时间，然后再加一个系统调用。

首先修改 kern/trap.c，加个 time tick （这里注释居然是 Lab 4: Your code here，神秘）

```c
// Handle clock interrupts. Don't forget to acknowledge the
// interrupt using lapic_eoi() before calling the scheduler!
// LAB 6: Your code here.

if (tf->tf_trapno == IRQ_OFFSET + IRQ_TIMER) {
    lapic_eoi();
    time_tick();
    sched_yield();
    return;
}
```

然后 syscall 里补系统调用，其实已经有 time_msec() 能用，直接包装就行

```c
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	// panic("sys_time_msec not implemented");
	return time_msec();
}
```

switch 里拦截一下

```c
case SYS_time_msec:
	return sys_time_msec();
```

然后 make INIT_CFLAGS=-DTEST_NO_NS run-testtime，-DTEST_NO_NS 是因为我们网络服务根本没写，如果直接起网络服务会 panic

![[MIT 6.828 Lab 6] 2](img\[MIT 6.828 Lab 6] 2.png)

count down 数数没问题

## The Network Interface Card

编写驱动程序需要深入了解硬件以及硬件向软件呈现的接口。实验说明会提供如何与 E1000 交互的高层概述，但在编写驱动时，你需要大量使用 Intel 的手册。

### Exercise 2

> 浏览 Intel 的 E1000 Software Developer's Manual。该手册涵盖了若干密切相关的以太网控制器。QEMU 仿真的是 82540EM。
>
> 现在你应该快速浏览第 2 章，以大致了解该设备。为了编写驱动程序，你需要熟悉第 3 章和第 14 章，以及 4.1 节（但不包括 4.1 的子节）。你还需要将第 13 章作为参考。其他章节主要涵盖 E1000 中你的驱动不必交互的组件。现在不必担心细节；只需要熟悉文档结构，以便之后能够找到所需内容。
>
> 阅读手册时，请记住 E1000 是一个复杂的设备，具备许多高级特性。一个可工作的 E1000 驱动只需要使用 NIC 提供的特性和接口中的一小部分。仔细思考与网卡交互的最简单方式。我们强烈建议你先让一个基础驱动工作起来，然后再利用高级特性。

请看：https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf

有个大概的轮廓即可🤔

2 章是总览，了解一下网卡是怎么和主板、内存交互的。3 章讲了网卡是怎么通过 DMA（直接内存访问） 和 描述符的环形队列来发送和接收数据包的。4.1 节是 PCI 接口基础。解释了设备的 Base Address Registers (BARs) 是什么，这是我们把硬件地址映射到内存的关键。13 章在讲寄存器的每一位的作用，纯参考资料，用得上的时候查一下。14 章讲硬件上电后怎么初始化。

## PCI Interface

E1000 是一个 PCI 设备，这意味着它插在主板的 PCI 总线上。PCI 总线具有地址线、数据线和中断线，允许 CPU 与 PCI 设备通信，也允许 PCI 设备读写内存。在使用 PCI 设备之前，需要先发现并初始化它。发现是指遍历 PCI 总线，寻找连接的设备。初始化是指分配 I/O 和内存空间，并为设备协商要使用的 IRQ 线。

我们已经在 `kern/pci.c` 中为你提供了 PCI 代码。为了在启动期间执行 PCI 初始化，PCI 代码会遍历 PCI 总线寻找设备。当它找到一个设备时，会读取其 vendor ID 和 device ID，并使用这两个值作为 key 在 `pci_attach_vendor` 数组中查找。该数组由如下形式的 `struct pci_driver` 项组成：

```c
struct pci_driver {
    uint32_t key1, key2;
    int (*attachfn) (struct pci_func *pcif);
};
```

如果发现的设备的 vendor ID 和 device ID 与数组中的某一项匹配，PCI 代码就会调用该项的 `attachfn` 来执行设备初始化。（设备也可以通过 class 识别，这也是 `kern/pci.c` 中另一个驱动表的用途。）

attach 函数会接收一个要初始化的 PCI function。一块 PCI 卡可以暴露多个 function，不过 E1000 只暴露一个。下面是 JOS 中表示 PCI function 的方式：

```c
struct pci_func {
    struct pci_bus *bus;

    uint32_t dev;
    uint32_t func;

    uint32_t dev_id;
    uint32_t dev_class;

    uint32_t reg_base[6];
    uint32_t reg_size[6];
    uint8_t irq_line;
};
```

上面的结构反映了开发者手册 4.1 节表 4-1 中的一些条目。`struct pci_func` 的最后三个字段对我们尤其重要，因为它们记录了为设备协商到的内存、I/O 和中断资源。`reg_base` 和 `reg_size` 数组包含最多六个 Base Address Registers（BAR）的信息。`reg_base` 存储内存映射 I/O 区域的基地址（或 I/O 端口资源的基 I/O 端口），`reg_size` 包含与 `reg_base` 中相应基址对应的字节大小或 I/O 端口数量，而 `irq_line` 包含分配给设备用于中断的 IRQ 线。E1000 各个 BAR 的具体含义在表 4-2 的后半部分给出。

当设备的 attach 函数被调用时，设备已经被发现但尚未启用。这意味着 PCI 代码还没有确定分配给该设备的资源，例如地址空间和 IRQ 线，因此 `struct pci_func` 结构的最后三个元素还没有填好。attach 函数应该调用 `pci_func_enable`，它会启用设备、协商这些资源，并填充 `struct pci_func`。

### Exercise 3

> 实现一个 attach 函数来初始化 E1000。在 `kern/pci.c` 的 `pci_attach_vendor` 数组中添加一项，使得在找到匹配的 PCI 设备时触发你的函数（务必把它放在标记表结束的 `{0, 0, 0}` 项之前）。你可以在 5.2 节中找到 QEMU 仿真的 82540EM 的 vendor ID 和 device ID。在 JOS 启动时扫描 PCI 总线时，你也应该能看到这些 ID 被列出。
>
> 目前，只需要通过 `pci_func_enable` 启用 E1000 设备。我们会在整个实验过程中逐步添加更多初始化。
>
> 我们已经为你提供了 `kern/e1000.c` 和 `kern/e1000.h` 文件，这样你就不必修改构建系统。它们目前是空的；你需要在本练习中填充它们。你可能还需要在内核中的其他位置包含 `e1000.h` 文件。
>
> 当你启动内核时，应该看到它打印出 E1000 网卡的 PCI function 已经被启用。你的代码现在应该能够通过 `make grade` 中的 pci attach 测试。

这里就是让内核在启动时能认出这块网卡，并把它激活。

网卡是插在主板 PCI 插槽上的。JOS 在启动时，会去遍历整个 PCI 总线，寻找挂载在上面的各种设备。每找到一个设备，就会去读它的 Vendor ID（厂商号） 和 Device ID（设备号）。如果这两个 ID 和我们在 kern/pci.c 里注册的驱动对上了，JOS 就会调用 attach() 来接管这个设备。

那我们首先得找到网卡的这两个号。Exercise 1 的 log 里能看见

```shell
PCI: 00:00.0: 8086:1237: class: 6.0 (Bridge device) irq: 0
PCI: 00:01.0: 8086:7000: class: 6.1 (Bridge device) irq: 0
PCI: 00:01.1: 8086:7010: class: 1.1 (Storage controller) irq: 0
PCI: 00:01.3: 8086:7113: class: 6.80 (Bridge device) irq: 9
PCI: 00:02.0: 1234:1111: class: 3.0 (Display controller) irq: 0
PCI: 00:03.0: 8086:100e: class: 2.0 (Network controller) irq: 11
```

8086:100e，对照一下 Intel 手册能发现，82540EM 这块网卡的 Vendor ID 是 0x8086，Device ID 是 0x100E

那接下来就得写这个 attach 了

在 kern/e1000.c 里写即可

```c
int
e1000_attach(struct pci_func *pcif)
{
	// 启用 PCI 设备，系统会自动配内存、IO 端口和 IRQ 中断号
	pci_func_enable(pcif);
	cprintf("E1000 attached.\n");
	
	return 0;
}
```

然后完善 kern/e1000.h

```c
#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>
int e1000_attach(struct pci_func *pcif);

#endif  // SOL >= 6
```

随后在 kern/pci.c 里

```c
// pci_attach_vendor matches the vendor ID and device ID of a PCI device. key1
// and key2 should be the vendor ID and device ID respectively
struct pci_driver pci_attach_vendor[] = {
	{ 0x8086, 0x100E, e1000_attach },  // E1000 网卡驱动
	{ 0, 0, 0 },
};
```

现在 make qemu 能看见 PCI enabled，以及我们 E1000 的 debug 语句

![[MIT 6.828 Lab 6] 3](img\[MIT 6.828 Lab 6] 3.png)

## Memory-mapped I/O

软件通过 memory-mapped I/O（MMIO）与 E1000 通信。你之前已经在 JOS 中见过两次：CGA 控制台和 LAPIC 都是你通过读写“内存”来控制和查询的设备。但这些读写并不会去往 DRAM；它们会直接到达这些设备。

`pci_func_enable` 会为 E1000 协商一个 MMIO 区域，并将其基址和大小存储在 BAR 0 中（也就是 `reg_base[0]` 和 `reg_size[0]`）。这是分配给设备的一段物理内存地址范围，这意味着你必须做一些工作，才能通过虚拟地址访问它。由于 MMIO 区域会被分配到很高的物理地址（通常高于 3GB），你不能使用 `KADDR` 访问它，因为 JOS 有 256MB 的限制。因此，你必须创建一个新的内存映射。我们将使用 `MMIOBASE` 之上的区域（你在 lab 4 中编写的 `mmio_map_region` 会确保我们不会覆盖 LAPIC 使用的映射）。由于 PCI 设备初始化发生在 JOS 创建用户环境之前，你可以在 `kern_pgdir` 中创建这个映射，并且它将始终可用。

### Exercise 4

> 在你的 attach 函数中，调用 `mmio_map_region`（你在 lab 4 中为支持 memory-mapping LAPIC 而编写的函数）为 E1000 的 BAR 0 创建一个虚拟内存映射。
>
> 你需要把该映射的位置记录在一个变量中，这样之后才能访问刚刚映射的寄存器。可以查看 `kern/lapic.c` 中的 `lapic` 变量，作为一种实现方式的示例。如果你确实使用指针指向设备寄存器映射，务必将其声明为 `volatile`；否则，编译器可以缓存值并重新排序对这块内存的访问。
>
> 要测试你的映射，可以尝试打印设备状态寄存器（13.4.2 节）。这是一个 4 字节寄存器，从寄存器空间的第 8 个字节开始。你应该得到 `0x80080783`，它表示全双工链路已启动、速率为 1000 MB/s 等信息。
>
> 提示：你将需要大量常量，例如寄存器位置和位掩码的值。试图从开发者手册中复制这些内容容易出错，而错误可能导致痛苦的调试过程。我们建议改用 QEMU 的 `e1000_hw.h` 头文件作为参考。我们不建议逐字复制它，因为它定义了远多于你实际需要的内容，并且可能不是以你需要的方式定义这些内容，但它是一个很好的起点。

我们刚刚用 pci_func_enable 激活了网卡，阅读源码，操作系统和网卡协商好了它需要的物理内存地址空间，并把基地址和长度保存在了 pcif->reg_base[0] 和 pcif->reg_size[0]（BAR 0）里。

由于分页机制，我们不能直接拿着这段物理地址去读写，此时就得用 mmio_map_region 把它映射到内核虚拟空间上。

这里注意，保存下来的 e1000 寄存器虚拟地址得加 volatile 关键字，如果不加，编译器看到你在连续读同一个变量，可能会直接用 CPU 寄存器里的缓存。但这是硬件状态寄存器，网卡收发数据的状态随时都在变，如果被编译器优化掉了就会导致读不到最新状态。

还有，手册上的寄存器地址都是字节偏移，要注意变量类型转换。

首先在 kern/e1000.h 中

```c
#include <kern/pci.h>

extern volatile uint32_t *e1000;

// Device Status Register, offset 8 bytes
#define E1000_STATUS   0x00008
```

然后在 kern/e1000.c 中

```c
#include <kern/e1000.h>
#include <kern/pmap.h>

// LAB 6: Your driver code here

volatile uint32_t *e1000;

int
e1000_attach(struct pci_func *pcif)
{
	// 启用 PCI 设备，系统会自动配内存、IO 端口和 IRQ 中断号
	pci_func_enable(pcif);
    // Device Status Register
    e1000 = (volatile uint32_t *) mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("E1000 status: 0x%08x\n", e1000[E1000_STATUS / sizeof(uint32_t)]);
	
	return 0;
}
```

现在 make qemu 可以看见 E1000 status 是 0x80080783，这代表网卡处于全双工模式，并且链路已经 up，之后就可以靠 e1000 直接收发包了。

![[MIT 6.828 Lab 6] 4](img\[MIT 6.828 Lab 6] 4.png)

## DMA

你可以想象通过写入和读取 E1000 的寄存器来发送和接收数据包，但这会很慢，并且要求 E1000 在内部缓冲数据包数据。相反，E1000 使用 Direct Memory Access（DMA）直接从内存中读写数据包数据，而不需要 CPU 参与。驱动负责为发送和接收队列分配内存，设置 DMA 描述符，并用这些队列的位置配置 E1000；之后的一切都是异步的。要发送一个数据包，驱动会把它复制到发送队列中的下一个 DMA 描述符中，并通知 E1000 有另一个数据包可用；当有时间发送该数据包时，E1000 会从描述符中复制数据。同样，当 E1000 接收到一个数据包时，它会把该数据包复制到接收队列中的下一个 DMA 描述符中，驱动可以在下一次有机会时从中读取。

接收队列和发送队列在高层上非常相似。两者都由一系列描述符组成。虽然这些描述符的具体结构不同，但每个描述符都包含一些标志，以及一个包含数据包数据的缓冲区的物理地址（要么是网卡要发送的数据包数据，要么是操作系统分配的、供网卡写入接收数据包的缓冲区）。

这些队列实现为循环数组，这意味着当网卡或驱动到达数组末尾时，会绕回到开头。两者都有 head 指针和 tail 指针，队列内容是这两个指针之间的描述符。硬件总是从 head 消耗描述符并移动 head 指针，而驱动总是向 tail 添加描述符并移动 tail 指针。发送队列中的描述符表示等待发送的数据包（因此，在稳态下，发送队列是空的）。对于接收队列，队列中的描述符是网卡可用于接收数据包的空闲描述符（因此，在稳态下，接收队列由所有可用接收描述符组成）。正确更新 tail 寄存器且不让 E1000 混乱是很棘手的；请小心！

指向这些数组的指针，以及描述符中数据包缓冲区的地址，都必须是物理地址，因为硬件会直接对物理 RAM 执行 DMA，而不会经过 MMU。

## Transmitting Packets

E1000 的发送和接收功能基本上相互独立，因此我们可以一次处理一个。我们将先处理数据包发送，原因很简单：在先发送一个“我在这里！”的数据包之前，我们无法测试接收。

首先，你必须按照 14.5 节中描述的步骤初始化网卡以进行发送（你不需要关心其子节）。发送初始化的第一步是设置发送队列。队列的精确结构在 3.4 节中描述，描述符的结构在 3.3.3 节中描述。我们不会使用 E1000 的 TCP offload 特性，因此你可以专注于 “legacy transmit descriptor format”。你现在应该阅读这些节，并熟悉这些结构。

## C Structures

你会发现使用 C 结构体描述 E1000 的结构会很方便。正如你在 `struct Trapframe` 这样的结构中见过的，C 结构体允许你精确地在内存中布局数据。C 可能会在字段之间插入填充，但 E1000 的结构布局应当不会造成问题。如果你确实遇到字段对齐问题，可以研究 GCC 的 `packed` 属性。

例如，考虑手册表 3-8 中给出的 legacy transmit descriptor，这里重新展示如下：

```text
  63            48 47   40 39   32 31   24 23   16 15             0
  +---------------------------------------------------------------+
  |                         Buffer address                        |
  +---------------+-------+-------+-------+-------+---------------+
  |    Special    |  CSS  | Status|  Cmd  |  CSO  |    Length     |
  +---------------+-------+-------+-------+-------+---------------+
```

结构的第一个字节位于右上角，因此要把它转换为 C 结构体，需要从右到左、从上到下阅读。如果你稍微眯着眼看，就会发现所有字段都恰好适配到标准大小的类型中：

```c
struct tx_desc
{
	uint64_t addr;
	uint16_t length;
	uint8_t cso;
	uint8_t cmd;
	uint8_t status;
	uint8_t css;
	uint16_t special;
};
```

你的驱动必须为发送描述符数组以及发送描述符指向的数据包缓冲区保留内存。有几种方式可以做到这一点，从动态分配页到简单地将它们声明为全局变量都可以。无论你选择哪种方式，请记住 E1000 会直接访问物理内存，这意味着它访问的任何缓冲区都必须在物理内存中连续。

处理数据包缓冲区也有多种方式。最简单的方式，也是我们建议你开始时采用的方式，是在驱动初始化期间为每个描述符预留一个数据包缓冲区，并简单地将数据包数据复制进出这些预分配缓冲区。以太网数据包的最大大小为 1518 字节，这限制了这些缓冲区需要多大。更复杂的驱动可以动态分配数据包缓冲区（例如，在网络使用率低时降低内存开销），甚至可以直接传递用户空间提供的缓冲区（这种技术称为 “zero copy”），但从简单方式开始是很好的。

### Exercise 5

> 执行 14.5 节中描述的初始化步骤（但不包括其子节）。使用第 13 节作为该初始化过程所涉及寄存器的参考，并使用 3.3.3 和 3.4 节作为发送描述符和发送描述符数组的参考。
>
> 注意发送描述符数组的对齐要求以及该数组长度的限制。由于 `TDLEN` 必须 128 字节对齐，而每个发送描述符为 16 字节，因此你的发送描述符数组需要包含 8 的倍数个发送描述符。不过，不要使用超过 64 个描述符，否则我们的测试将无法测试发送环溢出。
>
> 对于 `TCTL.COLD`，你可以假设全双工操作。对于 `TIPG`，请参考 13.4.34 节表 13-77 中描述的 IEEE 802.3 标准 IPG 默认值（不要使用 14.5 节中表里的值）。

评价为填空题，照着手册实现网卡的发送功能初始化

网卡发包时，我们不能让 CPU 等网卡一个字节一个字节地发出去，这里我们用 DMA，我们在内存里建一个环形发送队列。

这个队列里面存描述符，每个描述符 16 字节，里面记录数据包的物理地址以及包长等信息。我们还需要分配一大块内存来存放真正的数据包。按照手册 14.5，我们要把这个队列的物理地址告诉网卡（TDBAL、TDLEN 等寄存器），最后设置发包控制寄存器 TCTL 和包间隙寄存器 TIPG，并把队列的头尾指针TDH、TDT 清零。

这里要注意，硬件 DMA 只认物理地址，它不会过 MMU，所以根本不认识虚拟地址。而且，这块内存必须是物理连续的。JOS 中内核在启动时会被整体加载到一个连续的物理内存块中。所以，我们直接在 kern/e1000.c 里声明全局数组，它们就会被放在 .bss 段，这样就天生是物理连续的，直接用 PADDR() 就能发给硬件。

首先在 kern/e1000.h 中进行定义

```c
// Transmit 相关寄存器偏移量
#define E1000_TCTL     0x00400  // Transmit Control
#define E1000_TIPG     0x00410  // Transmit Inter-packet gap
#define E1000_TDBAL    0x03800  // TX Descriptor Base Address Low
#define E1000_TDBAH    0x03804  // TX Descriptor Base Address High
#define E1000_TDLEN    0x03808  // TX Descriptor Length
#define E1000_TDH      0x03810  // TX Descriptor Head
#define E1000_TDT      0x03818  // TX Descriptor Tail

// TCTL 寄存器配置位掩码
#define E1000_TCTL_EN       (1 << 1)      // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)      // Pad Short Packets
#define E1000_TCTL_CT       (0x10 << 4)   // Collision Threshold
#define E1000_TCTL_COLD     (0x40 << 12)  // Collision Distance (Full Duplex)

// 队列大小
#define TX_RING_SIZE  64
#define TX_PKT_SIZE   1518  // 以太网标准最大帧长

// 发送描述符结构体 16 bytes
struct tx_desc
{
	uint64_t addr;      // Buffer address
	uint16_t length;    // Packet length
	uint8_t cso;        // Checksum Offset
	uint8_t cmd;        // Command
	uint8_t status;     // Status
	uint8_t css;        // Checksum Start
	uint16_t special;
} __attribute__((packed));  // 防止编译器内存对齐

// 描述符状态和命令宏
#define E1000_TXD_STAT_DD  0x01   // Descriptor Done
#define E1000_TXD_CMD_EOP  0x01   // End of Packet
#define E1000_TXD_CMD_RS   0x08   // Report Status
```

然后在 kern/e1000.c 初始化

```c
#include <inc/string.h>

// 声明成全局数组，保证物理内存连续
struct tx_desc tx_ring[TX_RING_SIZE];
char tx_bufs[TX_RING_SIZE][TX_PKT_SIZE];

static void
e1000_tx_init()
{
	int i;
	memset(tx_ring, 0, sizeof(tx_ring));
	for (i = 0; i < TX_RING_SIZE; i++) {
		// 给每个描述符绑定一个存放数据的物理缓冲区
		tx_ring[i].addr = PADDR(tx_bufs[i]);
		// 标记为 Descriptor Done，表示网卡已发送完毕，目前空闲
		tx_ring[i].status = E1000_TXD_STAT_DD; 
	}

	// Base Addr 物理地址
	e1000[E1000_TDBAL / 4] = PADDR(tx_ring);
	e1000[E1000_TDBAH / 4] = 0;

    // Length
	e1000[E1000_TDLEN / 4] = sizeof(tx_ring);

	// Head & Tail
	e1000[E1000_TDH / 4] = 0;
	e1000[E1000_TDT / 4] = 0;

	// Transmit Control Register
	// EN: 开启 | PSP: 填充短包 | CT: 0x10 | COLD: 0x40 全双工
	e1000[E1000_TCTL / 4] = E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD;

	// Transmit Inter-packet Gap
	// IEEE 802.3：IPGT = 10, IPGR1 = 8, IPGR2 = 6
	e1000[E1000_TIPG / 4] = 10 | (8 << 10) | (6 << 20);
}

int
e1000_attach(struct pci_func *pcif)
{
	// 启用 PCI 设备，系统会自动配内存、IO 端口和 IRQ 中断号
	pci_func_enable(pcif);
    // Device Status Register
    e1000 = (volatile uint32_t *) mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("E1000 status: 0x%08x\n", e1000[E1000_STATUS / sizeof(uint32_t)]);
	e1000_tx_init();
	return 0;
}
```

现在 make E1000_DEBUG=tx,txerr qemu，没啥问题。tx disabled 是因为在 e1000_tx_init() 里我们先把 TDT 指针设为 0，然后才去配置 TCTL.EN 把网卡使能打开，后面那个则是后续的 Exercise

![[MIT 6.828 Lab 6] 5](img\[MIT 6.828 Lab 6] 5.png)

---

尝试运行 `make E1000_DEBUG=TXERR,TX qemu`。如果你使用的是课程版 qemu，当你设置 `TDT` 寄存器时，应该看到一条 `e1000: tx disabled` 消息（因为这发生在你设置 `TCTL.EN` 之前），之后不应再有其他 `e1000` 消息。

现在发送已经初始化，你需要编写发送数据包的代码，并通过系统调用让用户空间可以访问它。要发送一个数据包，你必须把它添加到发送队列的 tail，也就是将数据包数据复制到下一个数据包缓冲区中，然后更新 `TDT`（transmit descriptor tail）寄存器，通知网卡发送队列中又有一个数据包。（注意，`TDT` 是发送描述符数组中的索引，而不是字节偏移；文档对此不是很清楚。）

然而，发送队列大小有限。如果网卡发送数据包的速度落后，导致发送队列满了，会发生什么？为了检测这种情况，你需要来自 E1000 的一些反馈。不幸的是，你不能简单地使用 `TDH`（transmit descriptor head）寄存器；文档明确指出，从软件读取该寄存器是不可靠的。不过，如果你在发送描述符的 command 字段中设置 `RS` 位，那么当网卡已经发送该描述符中的数据包时，它会在该描述符的 status 字段中设置 `DD` 位。如果一个描述符的 `DD` 位被设置，你就知道可以安全地回收该描述符，并用它发送另一个数据包。

如果用户调用你的发送系统调用，但下一个描述符的 `DD` 位未设置，表明发送队列已满，那该怎么办？你必须决定如何处理这种情况。你可以简单地丢弃数据包。网络协议对此具有韧性，但如果你丢弃了一大批数据包，协议可能无法恢复。你也可以告诉用户环境它必须重试，就像你在 `sys_ipc_try_send` 中做的那样。这种方式的优点是会对产生数据的环境施加反压。

### Exercise 6

> 编写一个函数用于发送数据包：检查下一个描述符是否空闲，把数据包数据复制到下一个描述符，并更新 `TDT`。确保你处理发送队列已满的情况。

理一下逻辑，当我们想要发送一个数据包时，首先找队尾，读取网卡的 TDT 寄存器，这代表当前我们该把数据塞到环形队列的哪个索引。然后查状态，看一眼这个位置对应的描述符的 status 字段。如果它的 DD 位是 1，说明硬件之前已经把这个位置的数据发完了，这块内存现在是空闲的；如果 DD 是 0，说明硬件还在发或者队列已经满了被堵死了。讲义建议我们这里直接返回一个错误，让上层程序一会再来重试。随后复制数据，把我们要发的数据 memcpy 到 tx_bufs[tail] 里面去，配置命令，设置描述符的 length；设置 cmd，带上 EOP（End of Packet）和 RS（Report Status，告诉网卡发完之后把 DD 位置 1 报告回来），最后把 status 清零，把之前遗留的 DD 位抹掉，并更新指针，把 TDT 向后移一位，网卡硬件一看到 TDT 变了，就会自动通过 DMA 去拉内存里的数据发出去。

那么先实现发包，先声明

```c
int e1000_transmit(const void *data, size_t len);
```

然后

```c
int
e1000_transmit(const void *data, size_t len)
{
	uint32_t tail = e1000[E1000_TDT / 4];

	// 检查该描述符是否空闲：DD 位是否为 1
	if (!(tx_ring[tail].status & E1000_TXD_STAT_DD)) {
		// 队列满了，让用户态重试
		return -1;
	}

	// 限制发送长度
	if (len > TX_PKT_SIZE) {
		len = TX_PKT_SIZE;
	}
	memmove(tx_bufs[tail], data, len);

	tx_ring[tail].length = (uint16_t)len;
	// RS: 发送完设置 DD 位 | EOP: 这是一个完整包的结尾
	tx_ring[tail].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
	tx_ring[tail].status = 0;

	// 更新 TDT 寄存器，注意环形队列取模
	e1000[E1000_TDT / 4] = (tail + 1) % TX_RING_SIZE;

	return 0;
}
```

讲义建议我们自己写点数据测试，那就在 e1000_attach 后面加

```c
char *test = "MIT 6.828 By iPlayForSG";
e1000_transmit(test, 17);
e1000_transmit(test, 17);
```

make E1000_DEBUG=TXERR,TX qemu 能看见 e1000: index 0 和 e1000: index 1，这说明硬件捕获到了包	

![[MIT 6.828 Lab 6] 6](img\[MIT 6.828 Lab 6] 6.png)

tcpdump -XXnr qemu.pcap，我们写的字符串 MIT 6.828 By iPlayForSG 长度是 23，我们写的是 17，截断到 iPla，说明发包没有问题。

![[MIT 6.828 Lab 6] 7](img\[MIT 6.828 Lab 6] 7.png)

---

现在是测试你的数据包发送代码的好时机。尝试通过从内核中直接调用你的发送函数来发送几个数据包。为了测试，你不必创建符合任何特定网络协议的数据包。运行 `make E1000_DEBUG=TXERR,TX qemu` 来运行测试。发送数据包时，你应该看到类似以下内容：

```text
e1000: index 0: 0x271f00 : 9000002a 0
...
```

每一行给出发送数组中的索引、该发送描述符的缓冲区地址、`cmd/CSO/length` 字段，以及 `special/CSS/status` 字段。如果 QEMU 没有打印出你期望的发送描述符值，请检查你是否填充了正确的描述符，以及是否正确配置了 `TDBAL` 和 `TDBAH`。如果你得到 `e1000: TDH wraparound @0, TDT x, TDLEN y` 消息，这意味着 E1000 一直运行通过整个发送队列而没有停止（如果 QEMU 不检查这一点，它会进入无限循环），这很可能意味着你没有正确操作 `TDT`。如果你得到大量 `e1000: tx disabled` 消息，那么你没有正确设置 transmit control register。

一旦 QEMU 运行起来，你就可以运行 `tcpdump -XXnr qemu.pcap` 来查看你发送的数据包数据。如果你从 QEMU 看到了期望的 `e1000: index` 消息，但你的数据包捕获为空，请再次检查是否填充了发送描述符中每个必要的字段和位（E1000 可能已经遍历了你的发送描述符，但并不认为它必须发送任何内容）。

### Exercise 7

> 添加一个系统调用，让你可以从用户空间发送数据包。具体接口由你决定。不要忘记检查从用户空间传递到内核的任何指针。

首先在 inc/syscall.h 加一个 SYS_pkt_send，然后写 kern/syscall.c

```c
#include <kern/e1000.h>
...
    
static int
sys_pkt_send(void *buf, size_t len)
{
	// 检查内存是否属于用户空间，并且用户可读
	user_mem_assert(curenv, buf, len, PTE_U);
	return e1000_transmit(buf, len);
}
...
case SYS_pkt_send:
	return sys_pkt_send((void *)a1, (size_t)a2);
```

还要在 lib/syscall.c 里给用户态封装

```c
int
sys_pkt_send(void *buf, size_t len)
{
	return syscall(SYS_pkt_send, 1, (uint32_t)buf, (uint32_t)len, 0, 0, 0);
}
```

在 inc/lib.h 里声明

```c
int sys_pkt_send(void *buf, size_t len);
```

## Transmitting Packets: Network Server

现在你已经为设备驱动的发送端提供了系统调用接口，接下来就该发送数据包了。output 辅助环境的目标是在一个循环中执行以下操作：接受来自核心网络服务器的 `NSREQ_OUTPUT` IPC 消息，并使用你上面添加的系统调用，将这些 IPC 消息附带的数据包发送到网络设备驱动。`NSREQ_OUTPUT` IPC 由 `net/lwip/jos/jif/jif.c` 中的 `low_level_output` 函数发送，该函数将 lwIP 栈粘合到 JOS 的网络系统上。每个 IPC 都会包含一个 page，该 page 由一个 `union Nsipc` 组成，数据包位于其 `struct jif_pkt pkt` 字段中（见 `inc/ns.h`）。`struct jif_pkt` 如下：

```c
struct jif_pkt {
    int jp_len;
    char jp_data[0];
};
```

`jp_len` 表示数据包长度。IPC page 上之后的所有字节都专用于数据包内容。在结构末尾使用像 `jp_data` 这样的零长度数组，是一种常见的 C 技巧（有些人会说是丑陋做法），用于表示长度未预先确定的缓冲区。由于 C 不做数组边界检查，只要你确保该结构之后有足够的未使用内存，就可以像使用任意大小的数组一样使用 `jp_data`。

请注意，当设备驱动的发送队列中没有更多空间时，设备驱动、output 环境和核心网络服务器之间的交互。核心网络服务器使用 IPC 向 output 环境发送数据包。如果 output 环境由于发送数据包的系统调用而被挂起，因为驱动没有更多缓冲区空间容纳新数据包，那么核心网络服务器将阻塞，等待 output server 接受 IPC 调用。

### Exercise 8

> 实现 `net/output.c`。

看一眼讲义里那张绿色高亮的架构图。核心网络服务器本身不直接调用内核发包，它只负责组装好数据包，然后通过 IPC 把数据包扔给 output 环境。output 环境拿到数据包后，再调用我们刚才在 Exercise 7 写的 sys_pkt_send 系统调用发出去。如果网卡发送队列满了，sys_pkt_send 会失败或阻塞。这里如果让核心网络服务器自己去等，那整个系统的网络处理就全卡死了，让独立的 output 环境去阻塞重试，核心网络服务器就可以继续去干别的事情。

output.c 就是个死循环，ipc_recv 挂起等待来自核心网络服务器的 IPC 消息，IPC 会把一个包含 union Nsipc 的页面映射过来。数据包就在 nsipcbuf.pkt 里面，这是一个 struct jif_pkt，里面有 jp_len 和 jp_data。然后 sys_pkt_send 尝试调用系统调用发包，如果系统调用返回失败，就 sys_yield 让出 CPU，等会儿再一直 while 重试，直到发出去为止。

```c
envid_t whom;
int perm;
int32_t req;

while (1) {
    req = ipc_recv(&whom, &nsipcbuf, &perm);

    // 只处理发自核心网络环境，且类型为输出请求的消息
    if (req != NSREQ_OUTPUT || whom != ns_envid) {
        continue;
    }

    struct jif_pkt *pkt = &(nsipcbuf.pkt);
    while (sys_pkt_send(pkt->jp_data, pkt->jp_len) < 0) {
        sys_yield();
    }
}
```

make E1000_DEBUG=TXERR,TX NET_CFLAGS=-DTESTOUTPUT_COUNT=100 run-net_testoutput，注意 index 不能溢出

![[MIT 6.828 Lab 6] 8](img\[MIT 6.828 Lab 6] 8.png)

---

你可以使用 `net/testoutput.c` 在不涉及整个网络服务器的情况下测试你的 output 代码。尝试运行 `make E1000_DEBUG=TXERR,TX run-net_testoutput`。你应该看到类似以下内容：

```text
Transmitting packet 0
e1000: index 0: 0x271f00 : 9000009 0
Transmitting packet 1
e1000: index 1: 0x2724ee : 9000009 0
...
```

并且 `tcpdump -XXnr qemu.pcap` 应输出：

```text
reading from file qemu.pcap, link-type EN10MB (Ethernet)
-5:00:00.600186 [|ether]
	0x0000:  5061 636b 6574 2030 30                   Packet.00
-5:00:00.610080 [|ether]
	0x0000:  5061 636b 6574 2030 31                   Packet.01
...
```

要使用更大的数据包数量测试，可以尝试 `make E1000_DEBUG=TXERR,TX NET_CFLAGS=-DTESTOUTPUT_COUNT=100 run-net_testoutput`。如果这导致你的发送环溢出，请再次检查你是否正确处理了 `DD` status 位，以及是否已经通知硬件设置 `DD` status 位（通过使用 `RS` command 位）。

你的代码应该通过 `make grade` 的 testoutput 测试。

### Question 1

> 你是如何组织你的发送实现的？特别是，如果发送环已满，你会怎么做？

首先在内核里分配两个全局数组 tx_ring tx_bufs，它们在 .bss 所以保证内存连续。e1000_transmit 检查队尾描述符的状态，复制数据，并更新尾指针触发硬件发送。

在系统调用层封装 sys_pkt_send，在用户层让 output 作为一个独立的进程运行，通过 IPC 接收核心网络服务器发来的数据包，然后调用系统调用发出去。

如果队列已满，在内核层会直接返回 -1 到用户态，用户态接受到之后，先 sys_yield 让出 CPU 时间片，随后再尝试发送，直到成功发送。

## Part B: Receiving packets and the web server

## Receiving Packets

就像你为发送数据包所做的那样，你必须配置 E1000 来接收数据包，并提供一个接收描述符队列和接收描述符。3.2 节描述了数据包接收的工作方式，包括接收队列结构和接收描述符；初始化过程在 14.4 节中详细说明。

### Exercise 9

> 阅读 3.2 节。你可以忽略关于中断和 checksum offloading 的任何内容（如果你之后决定使用这些特性，可以再回到这些节），也不必关心阈值以及网卡内部缓存如何工作的细节。

阅读题，略，知道收包发包是完全反过来的即可。

---

接收队列与发送队列非常相似，不同之处在于它由等待被传入数据包填充的空数据包缓冲区组成。因此，当网络空闲时，发送队列是空的（因为所有数据包都已发送），但接收队列是满的（充满空数据包缓冲区）。

当 E1000 接收到一个数据包时，它首先检查该数据包是否匹配网卡配置的过滤器（例如，检查数据包是否发往这个 E1000 的 MAC 地址），如果不匹配任何过滤器就忽略该数据包。否则，E1000 会尝试从接收队列的 head 取出下一个接收描述符。如果 head（`RDH`）已经追上 tail（`RDT`），则接收队列没有空闲描述符，网卡会丢弃该数据包。如果存在空闲接收描述符，它会把数据包数据复制到该描述符指向的缓冲区中，设置描述符的 `DD`（Descriptor Done）和 `EOP`（End of Packet）status 位，并递增 `RDH`。

如果 E1000 接收到的数据包大于一个接收描述符中的数据包缓冲区，它会根据需要取出多个描述符，以存储整个数据包内容。为表示发生了这种情况，它会在所有这些描述符上设置 `DD` status 位，但只会在最后一个描述符上设置 `EOP` status 位。你可以在驱动中处理这种可能性，或者简单地配置网卡不接受 “long packets”（也称为 jumbo frames），并确保你的接收缓冲区足够大，能够存储最大可能的标准以太网数据包（1518 字节）。

### Exercise 10

> 按照 14.4 节中的过程设置接收队列并配置 E1000。你不必支持 “long packets” 或 multicast。目前不要配置网卡使用中断；如果你决定使用接收中断，之后可以修改。另外，配置 E1000 去除 Ethernet CRC，因为评分脚本期望 CRC 被去除。
>
> 默认情况下，网卡会过滤掉所有数据包。你必须用网卡自身的 MAC 地址配置 Receive Address Registers（`RAL` 和 `RAH`），以便接受发往该网卡的数据包。你可以简单地硬编码 QEMU 的默认 MAC 地址 `52:54:00:12:34:56`（我们已经在 lwIP 中硬编码了这个地址，因此在这里也这样做不会让事情变得更糟）。请非常小心字节序；MAC 地址按从低阶字节到高阶字节的顺序写入，因此 `52:54:00:12` 是 MAC 地址的低 32 位，而 `34:56` 是高 16 位。
>
> E1000 只支持一组特定的接收缓冲区大小（见 13.4.22 中对 `RCTL.BSIZE` 的描述）。如果你让接收数据包缓冲区足够大并禁用 long packets，就不必担心数据包跨越多个接收缓冲区。另外请记住，和发送一样，接收队列及数据包缓冲区都必须在物理内存中连续。
>
> 你应该至少使用 128 个接收描述符。

其实逻辑跟之前差不多。注意一下尾指针的处理，发包的时候，我们把 TDT 设为 0；但是收包的时候，如果把 RDH 和 RDT 都设为 0，网卡会认为队列已满。这里应该把 RDH 设为 0，但 RDT 设为最后一个可用描述符的索引。

首先 e1000.h

```c
// Receive 相关寄存器偏移量
#define E1000_RAL      0x05400  // Receive Address Low
#define E1000_RAH      0x05404  // Receive Address High
#define E1000_MTA      0x05200  // Multicast Table Array
#define E1000_RDBAL    0x02800  // RX Descriptor Base Address Low
#define E1000_RDBAH    0x02804  // RX Descriptor Base Address High
#define E1000_RDLEN    0x02808  // RX Descriptor Length
#define E1000_RDH      0x02810  // RX Descriptor Head
#define E1000_RDT      0x02818  // RX Descriptor Tail
#define E1000_RCTL     0x00100  // Receive Control

// RCTL 寄存器配置位掩码
#define E1000_RCTL_EN     (1 << 1)    // Receiver Enable
#define E1000_RCTL_BAM    (1 << 15)   // Broadcast Accept Mode
#define E1000_RCTL_SECRC  (1 << 26)   // Strip Ethernet CRC

// 队列大小
#define RX_RING_SIZE  128
#define RX_PKT_SIZE   2048

// 接收描述符结构体 16 bytes
struct rx_desc
{
	uint64_t addr;      // Buffer address
	uint16_t length;    // Packet length
	uint16_t csum;      // Checksum
	uint8_t status;     // Status
	uint8_t errors;     // Errors
	uint16_t special;
} __attribute__((packed));

// 接收状态位
#define E1000_RXD_STAT_DD       0x01    // Descriptor Done
#define E1000_RXD_STAT_EOP      0x02    // End of Packet
```

然后 e1000.c

```c
struct rx_desc rx_ring[RX_RING_SIZE];
char rx_bufs[RX_RING_SIZE][RX_PKT_SIZE];

static void
e1000_rx_init()
{
	int i;
	// 设置 MAC 52:54:00:12:34:56
	e1000[E1000_RAL / 4] = 0x12005452;
	// 设置高 16 位，并开启第 31 位 Address Valid, 0x80000000
	e1000[E1000_RAH / 4] = 0x5634 | 0x80000000;

	// 清空组播表 MTA
	for (i = 0; i < 128; i++) {
		e1000[(E1000_MTA / 4) + i] = 0;
	}

	memset(rx_ring, 0, sizeof(rx_ring));
	for (i = 0; i < RX_RING_SIZE; i++) {
		rx_ring[i].addr = PADDR(rx_bufs[i]);
		// 接收的时候不用设置状态位，硬件填满了会自动置位
	}

	//  Base Addr
	e1000[E1000_RDBAL / 4] = PADDR(rx_ring);
	e1000[E1000_RDBAH / 4] = 0;

    // Length
	e1000[E1000_RDLEN / 4] = sizeof(rx_ring);

	// Head & Tail，RDT 别置 0
	e1000[E1000_RDH / 4] = 0;
	e1000[E1000_RDT / 4] = RX_RING_SIZE - 1;

	// Receive Control
	// EN: 开启 | BAM: 接收广播 | SECRC: 硬件自动去除 CRC 校验和
	e1000[E1000_RCTL / 4] = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
}
```

最后 e1000_attach() 里调用一下 e1000_rx_init() 即可

make E1000_DEBUG=TX,TXERR,RX,RXERR,RXFILTER run-net_testinput 测试，能看见`e1000: unicast match[0]: 52:54:00:12:34:56`

---

现在即使还没有编写接收数据包的代码，你也可以对接收功能做一个基本测试。运行 `make E1000_DEBUG=TX,TXERR,RX,RXERR,RXFILTER run-net_testinput`。`testinput` 会发送一个 ARP（Address Resolution Protocol）announcement 数据包（使用你的数据包发送系统调用），QEMU 会自动回复。即使你的驱动还不能接收这个回复，你也应该看到一条 `e1000: unicast match[0]: 52:54:00:12:34:56` 消息，表示 E1000 收到了一个数据包，并且该数据包匹配配置的接收过滤器。如果你看到的是 `e1000: unicast mismatch: 52:54:00:12:34:56` 消息，则表示 E1000 过滤掉了该数据包，这说明你可能没有正确配置 `RAL` 和 `RAH`。确保字节顺序正确，并且没有忘记在 `RAH` 中设置 “Address Valid” 位。如果你没有得到任何 `e1000` 消息，那么你可能没有正确启用接收。

现在你已经准备好实现数据包接收了。要接收一个数据包，你的驱动必须跟踪它期望哪个描述符保存下一个接收到的数据包（提示：根据你的设计，E1000 中可能已经有一个寄存器在跟踪这个）。与发送类似，文档指出从软件读取 `RDH` 寄存器是不可靠的，因此为了确定某个数据包是否已经被交付到该描述符的数据包缓冲区，你必须读取描述符中的 `DD` status 位。如果 `DD` 位被设置，你可以把数据包数据从该描述符的数据包缓冲区复制出来，然后通过更新队列的 tail 索引 `RDT`，告诉网卡该描述符已经空闲。

如果 `DD` 位没有设置，那么说明没有收到数据包。这是接收端对应于发送队列满时的情况，你可以用几种方式处理。你可以简单地返回一个 “try again” 错误，并要求调用者重试。虽然这种方式对满发送队列效果很好，因为那是一个暂态条件，但对于空接收队列则不太合理，因为接收队列可能会长时间保持为空。第二种方式是挂起调用环境，直到接收队列中有数据包可处理。这种策略与 `sys_ipc_recv` 非常相似。就像 IPC 的情况一样，由于每个 CPU 只有一个内核栈，一旦离开内核，栈上的状态就会丢失。我们需要设置一个标志，表示某个环境因为接收队列下溢而被挂起，并记录系统调用参数。这种方式的缺点是复杂：必须指示 E1000 产生接收中断，并且驱动必须处理这些中断，以便恢复阻塞等待数据包的环境。

### Exercise 11

> 编写一个函数从 E1000 接收数据包，并通过添加一个系统调用将其暴露给用户空间。确保你处理接收队列为空的情况。

注意一下权限问题就行，发包的时候，内核只是读用户传来的内存，但是收包的时候，内核要把网卡里的数据写进用户传来的内存里。所以在检查权限的时候，要加上 PTE_W 权限。

e1000.h 中

```c
int e1000_receive(void *addr, size_t max_len);
```

e1000.c 中

```c
static uint32_t next_rx_idx = 0;

int
e1000_receive(void *addr, size_t max_len)
{
	// 检查当前描述符里有没有数据
	if (!(rx_ring[next_rx_idx].status & E1000_RXD_STAT_DD)) {
		return -1;
	}

	// 获取实际接收到的包长度
	size_t rx_len = rx_ring[next_rx_idx].length;
	if (rx_len > max_len) {
		rx_len = max_len;
	}

	// 将数据从 DMA 缓冲区复制到指定的内存 addr 中
	memmove(addr, rx_bufs[next_rx_idx], rx_len);

	rx_ring[next_rx_idx].status = 0;

	// 将网卡的 RDT 指针更新到刚刚读完的这个位置
	e1000[E1000_RDT / 4] = next_rx_idx;

	// index 往后推 1
	next_rx_idx = (next_rx_idx + 1) % RX_RING_SIZE;

	return rx_len;
}
```

然后就是 inc/syscall.h 加系统调用号

```c
SYS_pkt_recv,
```

kern/syscall.c 实现，注意权限 PTE_U | PTE_W

```c
static int
sys_pkt_recv(void *buf, size_t len)
{
	// 内核要往 buf 写数据，得有 PTE_W
	user_mem_assert(curenv, buf, len, PTE_U | PTE_W);
	return e1000_receive(buf, len);
}

...
    
    case SYS_pkt_recv:
		return sys_pkt_recv((void *)a1, (size_t)a2);
```

lib/syscall.c 封装

```c
int
sys_pkt_recv(void *buf, size_t len)
{
	return syscall(SYS_pkt_recv, 1, (uint32_t)buf, (uint32_t)len, 0, 0, 0);
}
```

inc/lib.h 声明

```c
int sys_pkt_recv(void *buf, size_t len);
```

这样就做完了。

## Receiving Packets: Network Server

在网络服务器的 input 环境中，你需要使用新的接收系统调用来接收数据包，并使用 `NSREQ_INPUT` IPC 消息将它们传递给核心网络服务器环境。这些 IPC input 消息应该附带一个 page，其中包含一个 `union Nsipc`，并且其 `struct jif_pkt pkt` 字段应填入从网络接收到的数据包。

### Exercise 12

> 实现 `net/input.c`。

这个跟 output 对称的

```c
// 为 nsipcbuf 申请一个物理页，确保进行 IPC 页面共享时，内存的权限配置完成
sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);

int len;
while (1) {
    // 从 DMA 环形队列里收一个包
    len = sys_pkt_recv(nsipcbuf.pkt.jp_data, 2048);

    if (len < 0) {
        sys_yield();
        continue;
    }
    nsipcbuf.pkt.jp_len = len;
    ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_U | PTE_W);
    // 下一轮使用
	sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);
}
```

现在 make grade 应该只有 web server [httpd] 是 FAIL，其它都是 OK

---

再次使用 `make E1000_DEBUG=TX,TXERR,RX,RXERR,RXFILTER run-net_testinput` 运行 testinput。你应该看到：

```text
Sending ARP announcement...
Waiting for packets...
e1000: index 0: 0x26dea0 : 900002a 0
e1000: unicast match[0]: 52:54:00:12:34:56
input: 0000   5254 0012 3456 5255  0a00 0202 0806 0001
input: 0010   0800 0604 0002 5255  0a00 0202 0a00 0202
input: 0020   5254 0012 3456 0a00  020f 0000 0000 0000
input: 0030   0000 0000 0000 0000  0000 0000 0000 0000
```

以 `input:` 开头的行是 QEMU 的 ARP 回复的十六进制转储。

你的代码应该通过 `make grade` 的 testinput 测试。注意，如果不先发送至少一个 ARP 数据包来通知 QEMU JOS 的 IP 地址，就无法测试数据包接收，因此你的发送代码中的 bug 可能会导致这个测试失败。

为了更彻底地测试你的网络代码，我们提供了一个名为 `echosrv` 的守护进程，它会设置一个运行在端口 7 上的 echo server，并把通过 TCP 连接发送给它的任何内容回显回来。在一个终端中使用 `make E1000_DEBUG=TX,TXERR,RX,RXERR,RXFILTER run-echosrv` 启动 echo server，在另一个终端中使用 `make nc-7` 连接它。你输入的每一行都应该被服务器回显。每当仿真的 E1000 接收到一个数据包时，QEMU 都应该在控制台打印类似以下内容：

```text
e1000: unicast match[0]: 52:54:00:12:34:56
e1000: index 2: 0x26ea7c : 9000036 0
e1000: index 3: 0x26f06a : 9000039 0
e1000: unicast match[0]: 52:54:00:12:34:56
```

此时，你还应该能够通过 echosrv 测试。

### Question 2

> 你是如何组织你的接收实现的？特别是，如果接收队列为空而某个用户环境请求下一个传入数据包，你会怎么做？

跟 Question 1 其实是一样的，只不过反过来而已。唯一需要注意的是每一轮循环完毕后都要`sys_page_alloc(0, &nsipcbuf, PTE_P | PTE_U | PTE_W);`分配新的页供下一轮循环使用。

## The Web Server

最简单形式的 web 服务器会将一个文件的内容发送给请求客户端。我们在 `user/httpd.c` 中为一个非常简单的 web 服务器提供了骨架代码。该骨架代码会处理传入连接并解析 header。

### Exercise 13

> web 服务器缺少处理将文件内容发回客户端的代码。通过实现 `send_file` 和 `send_data` 来完成 web 服务器。

在这个用户态的 httpd 看来，不管是磁盘上的文件还是网络连接，统统都是文件描述符。我们的核心网络栈 lwIP 接入了操作系统的 fd 系统，所以，向客户端发送网页，本质上就是从一个 fd 里面 read 数据，然后立刻 write 到另一个 fd（Socket）里去。

那么首先是 user/httpd.c 的 send_file，打开请求的文件 -> 查一下文件大小 -> 发送 HTTP 200 Header -> 调用 send_data 发送文件内容 -> 关闭文件。中间遇到任何找不到文件或访问目录的情况，直接 404。

```c
static int
send_file(struct http_request *req)
{
	int r;
	off_t file_size = -1;
	int fd;

	// open the requested url for reading
	// if the file does not exist, send a 404 error using send_error
	// if the file is a directory, send a 404 error using send_error
	// set file_size to the size of the file

	// LAB 6: Your code here.
	// panic("send_file not implemented");
	fd = -1;
	struct Stat st;

	// 以只读模式打开
	if ((fd = open(req->url, O_RDONLY)) < 0) {
		send_error(req, 404);
		r = fd; 
		goto end;
	}
	// 读取文件状态信息
	if ((r = fstat(fd, &st)) < 0) {
		send_error(req, 404);
		goto end;
	}

	// 检查这个路径是不是一个目录。
	if (st.st_isdir) {
		send_error(req, 404);
		r = -1;
		goto end;
	}

	file_size = st.st_size;

	if ((r = send_header(req, 200)) < 0)
		goto end;

	if ((r = send_size(req, file_size)) < 0)
		goto end;

	if ((r = send_content_type(req)) < 0)
		goto end;

	if ((r = send_header_fin(req)) < 0)
		goto end;

	r = send_data(req, fd);

end:
	if (fd >= 0) {
		close(fd);
	}
	return r;
}
```

然后就是 send_data，建一个缓冲区，把文件一点点读进来，然后往 Socket 里写，直到 read 返回 0，文件全部读完。

```c
static int
send_data(struct http_request *req, int fd)
{
	// LAB 6: Your code here.
	// panic("send_data not implemented");
    int r;
	char buf[1024];
    
	while ((r = read(fd, buf, sizeof(buf))) > 0) {
		if (write(req->sock, buf, r) != r) {
			return -1;
		}
	}

	if (r < 0) {
		return r;
	}

	return 0;
}
```

现在 make run-httpd-nox，然后在新终端 `curl http://localhost:26002/index.html`

![[MIT 6.828 Lab 6] 9](img\[MIT 6.828 Lab 6] 9.png)

make grade

![[MIT 6.828 Lab 6] 10](img\[MIT 6.828 Lab 6] 10.png)

---

完成 web 服务器后，启动 webserver（`make run-httpd-nox`），并用你喜欢的浏览器访问 `http://host:port/index.html`，其中 `host` 是运行 QEMU 的计算机名称（如果你在 Athena 上运行 QEMU，则使用 `hostname.mit.edu`，其中 `hostname` 是 Athena 上 `hostname` 命令的输出；如果你在同一台计算机上运行 web 浏览器和 QEMU，则使用 `localhost`），而 `port` 是 `make which-ports` 报告的 web server 端口号。你应该会看到由运行在 JOS 内部的 HTTP server 提供的网页。

此时，你应该能在 `make grade` 中获得 105/105。

### Question 3

> JOS 的 web 服务器提供的网页上写了什么？

```html
<html>
<head>
        <title>jhttpd on JOS</title>
</head>
<body>
        <center>
                <h2>This file came from JOS.</h2>
                <marquee>Cheesy web page!</marquee>
        </center>
</body>
</html>
```

> 你完成本实验大约花了多长时间？

小一天吧大概

---

完结撒花，接下来是 CS336 的学习