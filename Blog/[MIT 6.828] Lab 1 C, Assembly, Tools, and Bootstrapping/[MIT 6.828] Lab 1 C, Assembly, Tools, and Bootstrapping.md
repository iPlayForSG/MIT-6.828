2018 版的，不是课改后的 MIT 6.S081，课改前的好像更细致一些。之前学的都太零碎了，过一遍这个课程，用以构建整体的 OS 知识体系。

课程讲义：https://pdos.csail.mit.edu/6.828/2018/xv6/book-rev11.pdf，课程安排：https://pdos.csail.mit.edu/6.828/2018/schedule.html，我的环境：VMWare 17 Pro + Ubuntu 18.04

如果 mit 的页面访问不了，可以用 Web Archive https://web.archive.org/web/20250712101619/https://pdos.csail.mit.edu/6.828/2018/schedule.html

我的学习过程：https://github.com/iPlayForSG/MIT-6.828，可以看看 commit 记录。

Lab 1 讲义：https://pdos.csail.mit.edu/6.828/2018/labs/lab1/。实验分为 3 个部分，第一部分是熟悉 x86 汇编、QEMU 以及 PC 的启动引导程序。第二部分研究 6.828 内核引导加载程序，在 lab 的 boot 目录下，第三部分深入研究 6.828 内核的初始模板 JOS，在 kernel 目录下。

# Part 1: PC Bootstrap

## Getting Started with x86 assembly

就是让大家熟悉一下 x86 汇编之类的基础知识。这里提到，这个课程使用的汇编器是 GNU，语法为 AT&T。

### Exercise 1

这个 exercise 就是让我们去熟悉汇编，并且推荐了 AT&T 的参考资料 [Brennan's Guide to Inline Assembly](http://www.delorie.com/djgpp/doc/brennan/brennan_att_inline_djgpp.html)

## Simulating the x86

实际上是配环境。Software Setup 部分提供了一个叫 Athena 的服务器方便下载，但是我们并非 MIT 的学生，所以自己配环境吧。

MIT 给出的参考 https://pdos.csail.mit.edu/6.828/2018/tools.html，我参考的是 https://www.cnblogs.com/gatsby123/p/9746193.html 进行配置，注意 64 位的机子得安装 32 位支持库: `sudo apt-get install gcc-multilib`。编译 qemu 时的报错参考 https://github.com/woai3c/MIT6.828/blob/master/docs/install.md，评价是完美对上了。

另外，如果在编译 qemu 的时候有报错

```shell
install -d -m 0755 "/usr/local/share/qemu"
install: cannot change permissions of ‘/usr/local/share/qemu’: No such file or directory
Makefile:382: recipe for target 'install-datadir' failed
make: *** [install-datadir] Error 1
```

使用 sudo make install 即可

```shell
sed "s/localhost:1234/localhost:25000/" < .gdbinit.tmpl > .gdbinit
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::25000 -D qemu.log 
VNC server running on `127.0.0.1:5900'
6828 decimal is XXX octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

出现这个就是配置完成了，这个 kernel monitor 现在只支持 help 和 kerninfo 两个命令。退出 qemu 使用的命令为 `Ctrl+a x`

额外一个，如果想配 vs code 的 remote ssh，得退版本到老版本，新版本 vs code 不支持 Ubuntu 18.04 的 remote ssh，建议退到：https://update.code.visualstudio.com/1.85.2/win32-x64-user/stable，亲测可用。

另外是在新版本下 vs code 官方的解决方法：https://aka.ms/vscode-remote/faq/old-linux，有点麻烦。这里建议参考[这篇博客](https://www.hacktech.cn/post/2025/05/unable-to-connect-to-ubuntu-1804-after-vscode-199-z1vf6tf/)，我把解决方案抄在下面

> 最稳定的还是使用 linuxbrew。这里给出基于 linuxbrew 的解决方案，此处使用 bash 举例，如果你是 zsh 等环境，可以参见 [homebrew | 镜像站使用帮助 | 清华大学开源软件镜像站 | Tsinghua Open Source Mirror](https://mirrors.tuna.tsinghua.edu.cn/help/homebrew/)
>
> ```
> echo >> ~/.bashrc
> echo 'export HOMEBREW_BREW_GIT_REMOTE="https://mirrors.aliyun.com/homebrew/brew.git"' >> ~/.bashrc
> echo 'export HOMEBREW_CORE_GIT_REMOTE="https://mirrors.aliyun.com/homebrew/homebrew-core.git"' >> ~/.bashrc
> echo 'export HOMEBREW_API_DOMAIN="https://mirrors.aliyun.com/homebrew-bottles/api"' >> ~/.bashrc
> echo 'export HOMEBREW_BOTTLE_DOMAIN="https://mirrors.aliyun.com/homebrew/homebrew-bottles"' >> ~/.bashrc
> source ~/.bashrc
> export HOMEBREW_INSTALL_FROM_API=1
> # 从阿里云下载安装脚本并安装 Homebrew 
> git clone https://mirrors.aliyun.com/homebrew/install.git brew-install
> /bin/bash brew-install/install.sh
> rm -rf brew-install
> echo 'eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"' >> ~/.bashrc
> source ~/.bashrc
> ```
>
> 如果是海外环境，直接使用 `/bin/bash -c "$(curl -fsSL https://github.com/Homebrew/install/raw/master/install.sh)"` 即可
>
> 然后使用命令 `brew install patchelf` 安装 patchelf
>
> 然后执行如下命令
>
> ```
> echo 'VSCODE_SERVER_CUSTOM_GLIBC_PATH=/home/linuxbrew/.linuxbrew/opt/glibc/lib' >> ~/.ssh/environment
> echo 'VSCODE_SERVER_PATCHELF_PATH=/home/linuxbrew/.linuxbrew/bin/patchelf' >> ~/.ssh/environment
> echo 'VSCODE_SERVER_CUSTOM_GLIBC_LINKER=/home/linuxbrew/.linuxbrew/opt/glibc/lib/ld-linux-x86-64.so.2' >> ~/.ssh/environment
> sudo sed -i 's|#PermitUserEnvironment no|PermitUserEnvironment yes|g' /etc/ssh/sshd_config
> sudo systemctl restart sshd
> ```

## The PC's Physical Address Space

PC 的物理空间布局通常布局如下

![[MIT 6.828 Lab 1] 1](img\[MIT 6.828 Lab 1] 1.png)

第一台基于 16 位 Intel 8088 处理器的 PC 只能寻址 1MB 的物理内存，因此，早期 PC 的物理地址空间从0x00000000 开始，以 0x000FFFFF 而不是 0xFFFFFFFF 结束。标记为 "Low Memory" 的 640KB 区域是早期 PC 唯一可以使用的随机存取存储器（RAM），甚至用不完全这 1MB。

从 0x000A0000 到 0x000FFFFF 的 384KB 区域由硬件保留用于特殊用途，比如视频显示缓冲区（VGA Display）和非易失性存储器（NVM）中存储的固件。这其中最重要的就是基本输入/输出系统（BIOS），可以看见，在早期的 PC 中，BIOS 保存在只读存储器（ROM） 中，而现代 PC 将 BIOS 存储在可更新的闪存中。

> 提一句，ROM 是 NVM 的一种

BIOS 负责执行基本的系统初始化，例如激活显卡和检查系统内存。执行此初始化后，BIOS 会从某个适当的位置加载作系统，并将计算机的控制权传递给操作系统。

Intel 已经用分别支持 16MB 和 4GB 物理地址空间的 80286 和 80386 处理器突破了 1MB 的限制，为了实现向后兼容，所以还是保留了 1MB 物理地址空间的原始布局。所以，现代 PC 的物理内存中依然包含一片地址为 0x000A0000 ~0x00100000 的空白。这片空白将 RAM 分为了 "低内存" "传统内存"（0x00000000~0x000A0000，640 KB） 和 "扩展内存" （其它所有内存）两个部分。32 位处理器目前通常将物理地址空间最顶部的一些空间（特别是物理 RAM）交由 BIOS 使用。

我们将要实现的 JOS 由于设计上的限制，无论如何都只会使用物理地址的前 256 MB，也以此我们会假设所有的 PC 都只有一个 32 位的物理地址空间。但是我们应该知道，处理复杂的物理地址空间和多年来发展起来的硬件组织，是操作系统开发的重要实际挑战之一。

## The ROM BIOS

这一小节使用 QEMU 调试工具研究 IA-32 兼容计算机的启动方式。

来到 lab 文件夹下，make qemu-gdb

![[MIT 6.828 Lab 1] 2](img\[MIT 6.828 Lab 1] 2.png)

然后新建一个 terminal，依然是在 lab 文件夹下，运行 gdb

![[MIT 6.828 Lab 1] 3](img\[MIT 6.828 Lab 1] 3.png)

可以看见我的 gdb 并没有断在某个地址。看报错信息：

```shell
warning: File "/home/iplayforsg/Desktop/MIT_6.828/lab/.gdbinit" auto-loading has been declined by your `auto-load safe-path' set to "$debugdir:$datadir/auto-load".
To enable execution of this file add
	add-auto-load-safe-path /home/iplayforsg/Desktop/MIT_6.828/lab/.gdbinit
line to your configuration file "/home/iplayforsg/.gdbinit".
```

gdb 的安全机制 `auto-load safe-path` 导致其拒绝自动加载 `.gdbinit`。所以，我选择直接把 MIT_6.828 这个文件夹标记为安全：`echo "add-auto-load-safe-path /home/iplayforsg/Desktop/MIT_6.828/" >> ~/.gdbinit`

然后会发现 timeout，再观察第一个窗口，它的命令是 `qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log  -S`，是开在 26000 端口的，而第二个窗口 gdb 执行后它连接的是 25000 端口，所以把 lab 下的 `.gdbinit` 改一下，把连接的端口改成对应的就行。

![[MIT 6.828 Lab 1] 4](img\[MIT 6.828 Lab 1] 4.png)

可以看见，输出了

```assembly
The target architecture is assumed to be i8086
[f000:fff0]    0xffff0: ljmp   $0xf000,$0xe05b
0x0000fff0 in ?? ()
```

计算机启动后，从 0xFFFF0 执行命令，其分段地址 [CS:IP] = [F000:FFF0]，该地址位于为 ROM BIOS 保留的 64KB 区域的最顶部，，执行的命令是 ljmp。在 Lab 1 最开始的地方是说过使用的 AT&T 汇编，查一下可以知道 ljmp 是长跳转。ljmp section, offset，对应着就是跳转到 CS = 0xF000, IP = 0xE05B，也就是 0x10 * CS + IP = 0xFE05B

BIOS 被"硬连线"到物理地址 0x000f0000-0x000FFFFF，这种设计可确保 BIOS 在上电或任何系统重启后始终首先获得对机器的控制权，因为在上电时，机器 RAM 中没有其他软件可供处理器执行。QEMU 仿真器有自己的 BIOS，它将其放置在处理器模拟物理地址空间中的此位置。

### Exercise 2

> 使用 GDB 的 si（Step Instruction）命令跟踪到 ROM BIOS 中以获取更多指令，并尝试猜测它可能在做什么。

稍微理解一下就行。

接着之前提到的调试做，第一个语句是长跳转，那么一直跟到下一个长跳转去

```assembly
[f000:fff0]    0xffff0: ljmp   $0xf000,$0xe05b
[f000:e05b]    0xfe05b: cmpl   $0x0,%cs:0x6ac8
[f000:e062]    0xfe062: jne    0xfd2e1
[f000:e066]    0xfe066: xor    %dx,%dx
[f000:e068]    0xfe068: mov    %dx,%ss
[f000:e06a]    0xfe06a: mov    $0x7000,%esp
[f000:e070]    0xfe070: mov    $0xf34c2,%edx
[f000:e076]    0xfe076: jmp    0xfd15c
[f000:d15c]    0xfd15c: mov    %eax,%ecx
[f000:d15f]    0xfd15f: cli    
[f000:d160]    0xfd160: cld    
[f000:d161]    0xfd161: mov    $0x8f,%eax
[f000:d167]    0xfd167: out    %al,$0x70
[f000:d169]    0xfd169: in     $0x71,%al
[f000:d16b]    0xfd16b: in     $0x92,%al
[f000:d16d]    0xfd16d: or     $0x2,%al
[f000:d16f]    0xfd16f: out    %al,$0x92
[f000:d171]    0xfd171: lidtw  %cs:0x6ab8
[f000:d177]    0xfd177: lgdtw  %cs:0x6a74
[f000:d17d]    0xfd17d: mov    %cr0,%eax
[f000:d180]    0xfd180: or     $0x1,%eax
[f000:d184]    0xfd184: mov    %eax,%cr0
[f000:d187]    0xfd187: ljmpl  $0x8,$0xfd18f
```

这串指令是 CPU 从 16 位的实模式切换到 32 位的保护模式做的事情。

```assembly
[f000:e05b]    0xfe05b: cmpl   $0x0,%cs:0x6ac8
[f000:e062]    0xfe062: jne    0xfd2e1
```

这里先比较了`cs:0x6ac8` 处的值是否是 0，不为 0 就得跳转。这里一般就是 BIOS 写入的值，用来判断是进行冷启动还是热启动。如果是热启动，就不为 0 ，可以跳过一些自检步骤。

```assembly
[f000:e066]    0xfe066: xor    %dx,%dx
[f000:e068]    0xfe068: mov    %dx,%ss
[f000:e06a]    0xfe06a: mov    $0x7000,%esp
[f000:e070]    0xfe070: mov    $0xf34c2,%edx
[f000:e076]    0xfe076: jmp    0xfd15c
```

设置了一下 ss 和 esp 寄存器，建立了一个堆栈。ss 是堆栈段寄存器，随后直接跳转

```assembly
[f000:d15f]    0xfd15f: cli
[f000:d160]    0xfd160: cld
```

这里开始准备进入保护模式了。`cli`是 Clear Interrupt Flag，关闭所有可屏蔽中断。在切换到保护模式的过程中，GDT（Global Descriptor Table, 全局描述符） 和 IDT（Interrupt Descriptor Table, 中断描述符） 会被修改，这个时候如果发生了中断，CPU 跑去处理这个中断，那么它会发现系统处在一个不一致的中间态，然后就崩溃了。所以得关闭中断。

`cld`则是清除方向标志位`DF`，这个就是让后面 `movsb`之类的串操作的自增方向从低地址到高地址。

```assembly
[f000:d16b]    0xfd16b: in     $0x92,%al
[f000:d16d]    0xfd16d: or     $0x2,%al
[f000:d16f]    0xfd16f: out    %al,$0x92
```

这里跟 IO 端口 0x92 交互，读一个字节到 `al`寄存器，然后把它置 1，再写回去。这里是在打开 A20 门，在早期的 8086 CPU 上，地址线只有 20 条 (A0-A19)，只能访问 1MB 内存。为了兼容性，后续 CPU 默认也禁用第 21 根地址线 (A20)。要访问超过 1MB 的内存，就必须通过特定方式来开启 A20 地址线。

```assembly
[f000:d171]    0xfd171: lidtw  %cs:0x6ab8
[f000:d177]    0xfd177: lgdtw  %cs:0x6a74
```

这里就是在加载 IDT 和 GDT 了。

```assembly
[f000:d17d]    0xfd17d: mov    %cr0,%eax
[f000:d180]    0xfd180: or     $0x1,%eax
[f000:d184]    0xfd184: mov    %eax,%cr0
```

最后是操作 CR0 寄存器。CR0 的第 0 位是保护模式启用位 (Protection Enable, PE)，这里置 1 后 CPU 的寻址方式就从实模式变成保护模式了。

```assembly
[f000:d187]    0xfd187: ljmpl  $0x8,$0xfd18f
```

最后的长跳转，应该就是跳到内核里面操作去了。

这里的知识之前看 VT 的时候也有接触，可以发现现代 CPU 跟这个过程还是有很大部分共通的。

在初始化 PCI 总线和 BIOS 知道的所有重要设备后，它会搜索可启动设备，例如软盘、硬盘驱动器或 CD-ROM。最终，当它找到可启动磁盘时，BIOS 会从磁盘中读取 boot loader 并将控制权转移给它。

# Part 2: The Boot Loader

在 MIT 6.828 课程中，我们使用传统的启动机制，也就是说 Boot Loader 刚好 512 字节。Boot Loader 由一个汇编语言源文件 `boot/boot.S` 和一个 C 源文件 `boot/main.c` 组成。

Boot Loader 需要执行两个主要的功能：

* Boot Loader 将处理器从 16 位 实模式切换到 32 位保护模式，因为只有在这种模式下，软件才能访问处理器物理地址空间中 1MB 以上的所有内存。可以参考[这篇文章](https://www.cnblogs.com/fatsheep9146/p/5116426.html)去大概了解一下实模式和保护模式的原理与区别，或者看 [Intel 白皮书](https://www.intel.cn/content/www/cn/zh/content-details/843836/intel-64-and-ia-32-architectures-software-developer-s-manual-combined-volumes-3a-3b-3c-and-3d-system-programming-guide.html)也行。知道在保护模式下将分段地址`[segment:offset pairs]`转换为物理地址的情况不同，并且在转换后偏移量为 32 位而不是 16 位其实就勉强够用了。
* Boot Loader 可以通过 x86 的特殊 I/O 指令直接访问 IDE（Integrated Drive Electronics，电子集成驱动器） 磁盘设备寄存器，从硬盘读取内核。

`obj/boot/boot.asm` 是 GNU makefile 在编译 Boot Loader 后创建的 Boot Loader 的反汇编，`obj/kern/kernel.asm` 包含 JOS 内核的反汇编。这两个对调试很有帮助。

### Exercise 3 - 1

> 在地址 0x7c00 处设置断点。继续执行直到该断点。跟踪 boot/boot.S 中的代码，使用源代码和反汇编文件 obj/boot/boot.asm 来跟踪当前位置。同时使用 GDB 中的 x/i 命令反汇编引导加载程序中的指令序列，并将原始引导加载程序源代码与 obj/boot/boot.asm 中的反汇编代码和 GDB 进行比较。
>
> 跟踪到 boot/main.c 中的 bootmain() 函数，然后跟踪到 readsect() 函数。找到与 readsect() 函数中每条语句对应的汇编指令。跟踪 readsect() 函数的剩余部分，然后返回到 bootmain() 函数，找到用于从磁盘读取内核剩余扇区的 for 循环的起始和终止位置。找出循环结束时将运行的代码，在那里设置断点，然后继续执行到该断点。然后单步执行引导加载程序的剩余部分。

调就完事。断在 0x7C00 后，可以看见当前的汇编如下。

```assembly
(gdb) x/16i 0x7c00
=> 0x7c00:      cli    
   0x7c01:      cld    
   0x7c02:      xor    %ax,%ax
   0x7c04:      mov    %ax,%ds
   0x7c06:      mov    %ax,%es
   0x7c08:      mov    %ax,%ss
   0x7c0a:      in     $0x64,%al
   0x7c0c:      test   $0x2,%al
   0x7c0e:      jne    0x7c0a
   0x7c10:      mov    $0xd1,%al
   0x7c12:      out    %al,$0x64
   0x7c14:      in     $0x64,%al
   0x7c16:      test   $0x2,%al
   0x7c18:      jne    0x7c14
   0x7c1a:      mov    $0xdf,%al
   0x7c1c:      out    %al,$0x60
```

跟 boot.S 和 boot.asm 对比着看一下，可以发现 0x7C00 就是 Boot Loader 的起始地址，汇编其实也差不多，区别就在于 boot.S 内的汇编指令写清楚了表示长度的后缀，就是`xorw` 的`w`，`inb` 的`b`等。boot.asm 则是把有些名称直接用地址代替了。实际上都是一个意思。

boot.S 内的注释已经把每一行代码在干什么写得很清楚了。这里再分解一下吧

#### 16 位实模式初始化

```assembly
.globl start
start:
  .code16                     # Assemble for 16-bit mode
  cli                         # Disable interrupts
  cld                         # String operations increment

  # Set up the important data segment registers (DS, ES, SS).
  xorw    %ax,%ax             # Segment number zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment
```

`.code16` 是告诉汇编器，下面的指令是 16 位的，因为实模式是 16 位的。`cli` 关中断，`cld` 清除方向标志位。`xorw``movw`清空段寄存器。

#### 启用 A20 地址线

```assembly
# Enable A20:
  #   For backwards compatibility with the earliest PCs, physical
  #   address line 20 is tied low, so that addresses higher than
  #   1MB wrap around to zero by default.  This code undoes this.
seta20.1:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.1

  movb    $0xd1,%al               # 0xd1 -> port 0x64
  outb    %al,$0x64

seta20.2:
  inb     $0x64,%al               # Wait for not busy
  testb   $0x2,%al
  jnz     seta20.2

  movb    $0xdf,%al               # 0xdf -> port 0x60
  outb    %al,$0x60
```

首先等待键盘控制器输入缓冲区为空 (`testb $0x2, %al` 为 0)。随后发送 "write output port" 命令 (`0xd1`) 到命令端口 `0x64`。然后再次等待缓冲区为空。最后发送新的端口值 (`0xdf`) 到数据端口 `0x60`，这个值会打开 A20 门。

#### 切换到 32 位保护模式

```assembly
# Switch from real to protected mode, using a bootstrap GDT
# and segment translation that makes virtual addresses 
# identical to their physical addresses, so that the 
# effective memory map does not change during the switch.
lgdt    gdtdesc
movl    %cr0, %eax
orl     $CR0_PE_ON, %eax
movl    %eax, %cr0

# Jump to next instruction, but in 32-bit code segment.
# Switches processor into 32-bit mode.
ljmp    $PROT_MODE_CSEG, $protcseg
```

首先，定义了一个临时的 GDT，GDT 定义了内存段的基地址、大小和权限。gdt 包含了三个段描述符：一个空描述符、一个可执行可读的代码段 (`STA_X|STA_R`) 和一个可写的据段 (`STA_W`)。这两个段的基地址都是 0x0，大小都是 4GB (0xFFFFFFFF)，这样可以让虚拟地址 = 物理地址。gdtdesc 则描述了 GDT 的大小和地址。（这些最后的 GDT 定义里有）

`lgdt gdtdesc`指令将 GDT 的地址和大小加载到 CPU 的 GDTR 寄存器中。

然后，通过 CR0 开启保护模式，之前 Exercise 2 已经提过了。

```assembly
ljmp    $PROT_MODE_CSEG, $protcseg
```

这个长跳转清空所有在实模式下预取的指令，将 GDT 中的代码段选择子（.set PROT_MODE_CSEG, 0x8 \# kernel code segment selector）加载至 CS 寄存器，并跳转到 protcseg 标签处。

从此，CPU 开始在 32 位模式下，按照 GDT 的规则执行代码。

#### 32 位保护模式初始化

```assembly
  .code32                     # Assemble for 32-bit mode
protcseg:
  # Set up the protected-mode data segment registers
  movw    $PROT_MODE_DSEG, %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS
  movw    %ax, %ss                # -> SS: Stack Segment
  
  # Set up the stack pointer and call into C.
  movl    $start, %esp
  call bootmain

  # If bootmain returns (it shouldn't), loop.
spin:
  jmp spin
```

`.code32` 是告诉汇编器，下面的指令是 32 位的，因为保护模式是 32 位的。随后将 GDT 中定义的数据段选择子 （.set PROT_MODE_DSEG, 0x10 # kernel data segment selector）加载到所有的数据段寄存器和堆栈段寄存器 

然后设置栈顶 ESP，并且调用 C 语言的`bootmain`函数。这里就正式将控制权转交内核了。

最后还有一个`spin`，如果 `bootmain` 函数意外返回，则会在此处死循环，防止它执行后面的垃圾数据而导致崩溃。

#### GDT 定义

```assembly
# Bootstrap GDT
.p2align 2                                # force 4 byte alignment
gdt:
  SEG_NULL				# null seg
  SEG(STA_X|STA_R, 0x0, 0xffffffff)	# code seg
  SEG(STA_W, 0x0, 0xffffffff)	        # data seg

gdtdesc:
  .word   0x17                            # sizeof(gdt) - 1
  .long   gdt                             # address gdt
```

这里就是 GDT 的本体了。

`.p2align 2` 是按 2 的 2 次方对齐，也就是 4 字节对齐。这能确保 gdt 的起始地址是4的倍数，可以提升 CPU 访问效率。

`SEG_NULL` 定义了GDT的第一个表项（索引为0），它必须是一个空描述符。这是 x86 架构的硬性要求，用于在段寄存器未正确初始化时（值为0）引发异常，起到安全保护的作用。它的段选择子是 0x0（0 * 8 = 0）。

后面两个分别是代码段和数据段，STA_X|STA_R 是可读可执行，STA_W 是可写。两个 0x0 是段的基址，0xffffffff 是段的界限，也就是它们都从 0 开始，最大 4GB。它们的段选择子分别是 0x8 和 0x10（1 * 8 = 0x8, 2 * 8 = 0x10）。指令`ljmp $PROT_MODE_CSEG, $protcseg`中 \$PROT_MODE_CSEG 值为 8 ，`movw $PROT_MODE_DSEG, %ax`中 \$PROT_MODE_DSEG 值为 0x10，就是这里得来的。

gdtdesc 就是 lgdt 加载进 GDTR 的东西。0x17 = 23，这个 23 就是 GDT 大小 - 1，GDT 3 个表项，每个 8 字节。`.long` 定义一个 32 位长整型，值是 gdt 标签的线性基址。

### Exercise 3 - 2

接下来就是要跟踪到 `bootmain()`。

![[MIT 6.828 Lab 1] 5](img\[MIT 6.828 Lab 1] 5.png)

可以发现，函数地址是 0x7d15，这个可以在 boot.asm 内看见对应的汇编

![[MIT 6.828 Lab 1] 6](img\[MIT 6.828 Lab 1] 6.png)

然后是跟到 `readsect`函数

这个函数在`readseg`内，首先是调用了 `waitdisk`函数，等待磁盘。

```assembly
(gdb) x/16i 0x7c6a
=> 0x7c6a:      push   %ebp
   0x7c6b:      mov    $0x1f7,%edx
   0x7c70:      mov    %esp,%ebp
   0x7c72:      in     (%dx),%al
   0x7c73:      and    $0xffffffc0,%eax
   0x7c76:      cmp    $0x40,%al
   0x7c78:      jne    0x7c72
   0x7c7a:      pop    %ebp
   0x7c7b:      ret   
```

这个就是 `waitdisk`，boot.asm 中也给出了其相当于`while ((inb(0x1F7) & 0xC0) != 0x40)`。`inb(0x1F7)`是从 IO 端口 0x1F7 读取一个字节，这个端口是 IDE 硬盘控制器标准的状态寄存器，读取到的这个字节包含了硬盘当前状态的多个标志位。0xC0 = 11000000，也就是说这里在检查第 6 7 位的状态。0x40 = 01000000，目标状态就是第 7 位为 0 ，第 6 位为 1。在 IDE 状态寄存器中，第七位（BSY）为 1 时表示驱动器正忙，无法接收新命令；第 6 位（DRDY）为 1 时，表示驱动器已就绪，可以接收新命令。

```assembly
(gdb) x/40i 0x7c7c
   0x7c7c:      push   %ebp
   0x7c7d:      mov    %esp,%ebp
   0x7c7f:      push   %edi
   0x7c80:      mov    0xc(%ebp),%ecx
   0x7c83:      call   0x7c6a
=> 0x7c88:      mov    $0x1,%al
   0x7c8a:      mov    $0x1f2,%edx
   0x7c8f:      out    %al,(%dx)
   0x7c90:      mov    $0x1f3,%edx
   0x7c95:      mov    %cl,%al
   0x7c97:      out    %al,(%dx)
   0x7c98:      mov    %ecx,%eax
   0x7c9a:      mov    $0x1f4,%edx
   0x7c9f:      shr    $0x8,%eax
   0x7ca2:      out    %al,(%dx)
   0x7ca3:      mov    %ecx,%eax
   0x7ca5:      mov    $0x1f5,%edx
   0x7caa:      shr    $0x10,%eax
   0x7cad:      out    %al,(%dx)
   0x7cae:      mov    %ecx,%eax
   0x7cb0:      mov    $0x1f6,%edx
   0x7cb5:      shr    $0x18,%eax
   0x7cb8:      or     $0xffffffe0,%eax
   0x7cbb:      out    %al,(%dx)
   0x7cbc:      mov    $0x20,%al
   0x7cbe:      mov    $0x1f7,%edx
   0x7cc3:      out    %al,(%dx)
   0x7cc4:      call   0x7c6a
   0x7cc9:      mov    0x8(%ebp),%edi
   0x7ccc:      mov    $0x80,%ecx
   0x7cd1:      mov    $0x1f0,%edx
   0x7cd6:      cld    
   0x7cd7:      repnz insl (%dx),%es:(%edi)
   0x7cd9:      pop    %edi
   0x7cda:      pop    %ebp
   0x7cdb:      ret
```

这一串是`readsect`整个函数。整体来说，它实现了从硬盘的指定位置读取一个完整的扇区（512 字节）到内存中。

首先执行到`call   0x7c6a`，这里是调用 `waitdisk`，确保硬盘控制器处于就绪状态。接下来做的就如之前提到的 Boot Loader 两个主要功能中的第二个：通过 x86 的特殊 I/O 指令直接访问 IDE 磁盘设备寄存器。

> 常见的 I/O 端口地址和其功能如下：
>
> - **0x1F0**: 数据寄存器，用于读写磁盘数据。
> - **0x1F1**: 错误寄存器（读）或功能寄存器（写）。
> - **0x1F2**: 扇区计数寄存器，指定要操作的扇区数。
> - **0x1F3**: 扇区号寄存器，指定要操作的扇区号（LBA模式下）。
> - **0x1F4**: 柱面低字节寄存器，指定扇区所在的柱面号低8位。
> - **0x1F5**: 柱面高字节寄存器，指定扇区所在的柱面号高8位。
> - **0x1F6**: 驱动器/磁头寄存器，用于选择驱动器和磁头。
> - **0x1F7**: 状态寄存器（读）或命令寄存器（写），用于检查硬盘状态或发送命令。

```assembly
0x7c88:    mov    $0x1,%al
0x7c8a:    mov    $0x1f2,%edx
0x7c8f:    out    %al,(%dx)
```

向端口 0x1F2（扇区计数寄存器）发送 1，表示要读取 1 个扇区。

```assembly
0x7c90:    mov    $0x1f3,%edx
0x7c95:    mov    %cl,%al
0x7c97:    out    %al,(%dx)
```

将 offset 的最低 8 位（存放在CL）发送到端口 0x1F3（扇区号寄存器），也就是 LBA（Logical Block Addressing，逻辑块寻址） 地址 0-7 位。

```assembly
0x7c98:    mov    %ecx,%eax
0x7c9f:    shr    $0x8,%eax
0x7ca2:    out    %al,(%dx)
```

将 offset 右移 8 位，把中间 8 位（LBA 地址 8-15位）发送到端口 0x1F4（柱面低字节寄存器）。

```assembly
0x7ca3:    mov    %ecx,%eax
0x7caa:    shr    $0x10,%eax
0x7cad:    out    %al,(%dx)
```

将 offset 右移 16 位，把次高 8 位（LBA 地址 16-23 位）发送到端口 0x1F5（柱面高字节寄存器）。

```assembly
0x7cae:    mov    %ecx,%eax
0x7cb5:    shr    $0x18,%eax
0x7cb8:    or     $0xffffffe0,%eax
0x7cbb:    out    %al,(%dx)
```

将 offset 右移 24 位得到最高的 4 位，然后 or 上 0xE0。最终的字节为 1110XXXX 的形式，表示使用 LBA 模式，并选择主驱动器。将结果发送到端口 0x1F6（驱动器/磁头寄存器）。

```assembly
0x7cbc:    mov    $0x20,%al
0x7cbe:    mov    $0x1f7,%edx
0x7cc3:    out    %al,(%dx)
0x7cc4:    call   0x7c6a
```

向端口 0x1F7 （状态寄存器（读）或命令寄存器（写）） 发送 0x20，这是读取扇区的命令码。随后硬盘开始工作。然后再调用 `waitdisk`，等待硬盘完成寻道和读取，并将数据放在其内部缓冲区，让 CPU 进行读取。

boot.asm 中依然已经给出了汇编对应的源码，它们相当于：

```c
outb(0x1F2, 1);		// count = 1
outb(0x1F3, offset);
outb(0x1F4, offset >> 8);
outb(0x1F5, offset >> 16);
outb(0x1F6, (offset >> 24) | 0xE0);
outb(0x1F7, 0x20);	// cmd 0x20 - read sectors
// wait for disk to be ready
waitdisk();
```

最终 CPU 读取刚刚从硬盘取出的数据

```assembly
0x7cc9:    mov    0x8(%ebp),%edi
0x7ccc:    mov    $0x80,%ecx
0x7cd1:    mov    $0x1f0,%edx
0x7cd6:    cld   
0x7cd7:    repnz insl (%dx),%es:(%edi)
```

从堆栈获取第一个参数 dst (目标内存地址)，存入 edi，设置计数器 ecx 为 0x80 = 128，edx 指向端口 0x1F0（数据寄存器），清除方向标志位，确保 edi 在每次操作后递增。随后重复 128 次 insl，这个操作从端口 0x1F0 读取一个 4 字节的长整数，并存入 edi 指向的内存地址，也就是`insl(0x1F0, dst, SECTSIZE/4);`。128 次循环后总共从缓冲区读取了 128 * 4 = 512 字节的数据。整个操作刚好读取了一个扇区的数据至内存中。

Exercise 还要求确定从磁盘读取内核其余扇区的 for 循环的开始和结束。

```assembly
(gdb) x/16i 0x7d51
=> 0x7d51:      cmp    %esi,%ebx
   0x7d53:      jae    0x7d6b
   0x7d55:      pushl  0x4(%ebx)
   0x7d58:      pushl  0x14(%ebx)
   0x7d5b:      add    $0x20,%ebx
   0x7d5e:      pushl  -0x14(%ebx)
   0x7d61:      call   0x7cdc
   0x7d66:      add    $0xc,%esp
   0x7d69:      jmp    0x7d51
   0x7d6b:      call   *0x10018
```

这里相当于

```c
for (; ph < eph; ph++)
    readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
((void (*)(void)) (ELFHDR->e_entry))();
```

执行循环后，跳入 ELF 文件的入口点，就准备开始执行内核代码了。

```assembly
(gdb) c
Continuing.
=> 0x7d6b:      call   *0x10018

Breakpoint 4, 0x00007d6b in ?? ()
(gdb) si
=> 0x10000c:    movw   $0x1234,0x472
0x0010000c in ?? ()
(gdb) x/16i 0x10000c
=> 0x10000c:    movw   $0x1234,0x472
   0x100015:    mov    $0x112000,%eax
   0x10001a:    mov    %eax,%cr3
   0x10001d:    mov    %cr0,%eax
   0x100020:    or     $0x80010001,%eax
   0x100025:    mov    %eax,%cr0
   0x100028:    mov    $0xf010002f,%eax
   0x10002d:    jmp    *%eax
   0x10002f:    mov    $0x0,%ebp
   0x100034:    mov    $0xf0110000,%esp
   0x100039:    call   0x1000a6
   0x10003e:    jmp    0x10003e
   0x100040:    push   %ebp
   0x100041:    mov    %esp,%ebp
   0x100043:    push   %esi
   0x100044:    push   %ebx
(gdb) si
=> 0x100015:    mov    $0x112000,%eax
0x00100015 in ?? ()
(gdb) 
=> 0x10001a:    mov    %eax,%cr3
0x0010001a in ?? ()
(gdb) 
=> 0x10001d:    mov    %cr0,%eax
0x0010001d in ?? ()
(gdb) 
=> 0x100020:    or     $0x80010001,%eax
0x00100020 in ?? ()
(gdb) 
=> 0x100025:    mov    %eax,%cr0
0x00100025 in ?? ()
(gdb) 
=> 0x100028:    mov    $0xf010002f,%eax
0x00100028 in ?? ()
(gdb) 
=> 0x10002d:    jmp    *%eax
0x0010002d in ?? ()
(gdb) 
=> 0xf010002f <relocated>:      mov    $0x0,%ebp
relocated () at kern/entry.S:74
74              movl    $0x0,%ebp                       # nuke frame pointer
(gdb) 
=> 0xf0100034 <relocated+5>:    mov    $0xf0110000,%esp
relocated () at kern/entry.S:77
77              movl    $(bootstacktop),%esp
(gdb) 
=> 0xf0100039 <relocated+10>:   call   0xf01000a6 <i386_init>
80              call    i386_init
(gdb) 
=> 0xf01000a6 <i386_init>:      push   %ebp
i386_init () at kern/init.c:24
24      {
(gdb) 
=> 0xf01000a7 <i386_init+1>:    mov    %esp,%ebp
0xf01000a7      24      {
```

接下来就会进入 `kern/entry.S` 和`kern/init.c`了

现在我们回答 Exercise 3 提出的 4 个问题

> 处理器从什么时候开始执行 32 位代码？究竟是什么原因导致从 16 位模式切换到 32 位模式？

之前已经提过几次了：CR0 寄存器置 1 的时候就开始执行 32 位代码了。

> 执行的 boot loader 的最后一条指令是什么，它刚刚加载的内核的第一条指令是什么？

最后一条指令是`call   *0x10018`，内核的第一条指令是`movw   $0x1234,0x472`

> 内核的第一条指令在哪里 ？

指令地址是 0x10000c 

> Boot Loader 如何决定它必须读取多少个扇区才能从磁盘获取整个内核？它在哪里可以找到这些信息？

显然，因为内核是个 ELF 文件，这些信息都可以在 ELF 头中找到。JOS 中的相关代码如下，位于 boot/main.c 的 `bootmain` 函数。

```c
ph = (struct Proghdr *) ((uint8_t *) ELFHDR + ELFHDR->e_phoff);
eph = ph + ELFHDR->e_phnum;
for (; ph < eph; ph++)
    readseg(ph->p_pa, ph->p_memsz, ph->p_offset);
```

## Loading the Kernel

本节将查看引导加载程序的 C 语言部分`boot/main.c`

### Exercise 4

这里让我们回顾 C 语言基础

> 阅读关于C语言的指针部分的知识。最好的参考书是"The C Programming Language"。
>
> 阅读5.1到5.5节。然后下载 pointers.c 的代码，并且编译运行它，确保你理解在屏幕上打印出来的所有的值是怎么来的。尤其要重点理解第1行，第6行的指针地址是如何得到的，以及在第2行到第4行的值是如何得到的，还有为什么在第5行打印出来的值看起来像程序崩溃了。
>
> Waring：除非你已经对C语言相当了解，否则千万不要跳过这一个练习。

读就算了，看看代码得了。

```c
#include <stdio.h>
#include <stdlib.h>

void f(void)
{
    int a[4];
    int *b = malloc(16);
    int *c;
    int i;

    printf("1: a = %p, b = %p, c = %p\n", a, b, c);

    c = a;
    for (i = 0; i < 4; i++) a[i] = 100 + i;
    c[0] = 200;
    printf("2: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c[1] = 300;
    *(c + 2) = 301;
    3[c] = 302;
    printf("3: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c = c + 1;
    *c = 400;
    printf("4: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    c = (int *) ((char *) c + 1);
    *c = 500;
    printf("5: a[0] = %d, a[1] = %d, a[2] = %d, a[3] = %d\n",
	   a[0], a[1], a[2], a[3]);

    b = (int *) a + 1;
    c = (int *) ((char *) a + 1);
    printf("6: a = %p, b = %p, c = %p\n", a, b, c);
}

int main(int ac, char **av)
{
    f();
    return 0;
}
```

跑出来是这样

![[MIT 6.828 Lab 1] 7](img\[MIT 6.828 Lab 1] 7.png)

第一行：a 是数组的首地址`&a[0]`，它是 f 函数的局部变量，所以被分配在栈上。b 是 malloc 分配的空间的起始地址，它分配在堆上，明显看得出来它的地址比栈要低得多。c 是个没有初始化的垃圾指针，所以这个值是什么都有可能。

第二行：`c = a`后，c 指向 a 的首地址，现在它们指向同一块内存。for 给 a 赋值，`c[0] = 200` 修改了值，相应地，`a[0]` 也变了。

第三行：`a[0]` 没变，`c[1] = 300` 修改了 `a[1]`，`*(c + 2)`相当于是`c[2]`，`3[c]`这个写法很神秘，查了一下资料，C 的编译器会把`x[y]`解释成`*(x + y)`，所以这里就是`*(3 + c)`

第四行：`c = c + 1`后 c 指向`a[1]`

第五行：这里为什么值有问题呢，是因为内存没有对齐。c 最开始指向的是 `a[1]`，`c = (int *) ((char *) c + 1)`，按顺序，先`(char *)`强制把整型指针变成字符指针，那么`+1` 就只会 + 1 个字节。这个时候再`(int *)`转回整型，那现在就未对齐了，指针指向的是`a[1]`的第二个字节。随后`*c = 500`，500 = 0x1F4，小端序是 F4 01 00 00，那`a[1]`变成 90 F4 01 00，0x0001F490 = 128144，`a[2]`的第一个字节又被 00 盖掉，就是 00 01 00 00，0x00000100 = 256

第六行：a 是首地址，b 加了个 `sizeof(int)`，4 字节，c 跟第五行是一样的，只加 1 字节。

Exercise 4 结束，下面继续 Loading the Kernel 部分

---

为了弄清楚`boot/main.c`，我们首先得理解什么是 ELF 二进制文件。在编译并且链接像 JOS 内核这样的 C 语言程序时，编译器会将每个 .c 源文件转换为一个对象 .o 文件，这个.o 内包含的是机器能够理解的汇编指令，然后链接器把所有编译的 .o 文件组合成一个二进制映像文件，例如`obj/kern/kernal`。在这里，这个二进制映像文件就是 ELF（Executable and Linkable Format，可执行和可链接格式）文件。

在 6.828 的课程中，我们简单地将 ELF 可执行文件视为几部分：首先是包含加载信息的头文件，它的后面跟着几个程序段，每个程序段都是一段连续的代码或数据，旨在加载到内存的指定地址。 boot loader 不会修改这些代码或数据，它会将其加载到内存中并开始执行。

ELF 文件以固定长度的 ELF 头开头（MAGIC NUMBER），后面跟着一个可变长度的程序头，这个程序头列出了每个要加载到内存中的程序段。这些 ELF 头的 C 定义位于 `inc/elf.h` 文件中。

这里我们主要关注这几个程序段：

* .text: 存放程序可执行代码
* .rodata: 存放只读数据，比如说 C 编译器生成的字符串常量
* .data: 存放程序已初始化的数据，比如全局变量 `int x = 5`

当链接器计算程序的内存布局时，它会在内存中紧跟 .data 之后的 .bss 段中为未初始化的全局变量（例如 `int x`）预留空间。C 语言要求未初始化的全局变量的值为 0。因此，ELF 中无需存储 .bss 的内容（都是 0）；链接器只需记录地址和 .bss 段的大小即可。加载器或程序本身必须将 .bss 段清零。

可以用以下命令来检查内核可执行文件中所有段的名称、大小、和链接地址

```shell
objdump -h obj/kern/kernel
```

![[MIT 6.828 Lab 1] 8](img\[MIT 6.828 Lab 1] 8.png)

可以看见，除了我们刚才提到过的 .text .rodata .data .bss 以外，还有一些其它的段。这些其它的段一般保存调试信息，并且只存在于程序的可执行文件中而不会加载进内存。

注意一下 .text 段的 VMA（链接地址）和 LMA（加载地址）。段的加载地址是指该段应该加载到内存中的地址，链接地址则是其被存放到的逻辑地址。

一般来说，链接地址和加载地址是一样的。比如说我们看一下 boot loader 的 .text，

```shell
objdump -h obj/boot/boot.out
```

![[MIT 6.828 Lab 1] 9](img\[MIT 6.828 Lab 1] 9.png)



boot loader 会用 ELF 程序头来决定如何加载段，这个程序头会指明 ELF 的哪些部分需要加载到内存中，以及每个部分所对应的地址。

```shell
objdump -x obj/kern/kernel
```

该命令可以检查 kernel 的程序头，当然下面还有段和符号表的信息，打印出来看一下就知道了。

![[MIT 6.828 Lab 1] 10](img\[MIT 6.828 Lab 1] 10.png)

可以看见程序头列出的是所有被加载到内存中的段的信息，每个表项都代表一个段，包含了非常详细的信息。这些需要被加载到内存中的段标记为 LOAD。

我们以第一个段解释一下：

* LOAD：需要被加载到内存的段
* off 0x00001000：在 kernel 文件中的起始偏移量是 0x1000
* vaddr 0xf0100000：虚拟地址，在分页机制下被映射到的虚拟内存地址
* paddr 0x00100000：物理地址，boot loader 应该将该段加载到物理内存 0x100000 的位置。物理地址就是在内存中实际存放的位置
* align 2**12：要求该段在内存中按 2^12 = 4096 字节（也就是一个页的大小）对齐
* filesz 0x0000759d：该段在文件中的大小
* memsz 0x0000759d: 该段在内存中占据的大小，与 filesz 相等
* flags r-x：该段的权限是 rx，可读可执行

顺便，可读可执行基本上能确定是 .text 段。

现在回到 boot/main.c 上来。每个程序头的 ph->p_pa 字段会包含段的目标物理地址。

BIOS 将引导扇区加载到从地址 0x7c00 开始的内存中，这是引导扇区的加载地址，也是它的链接地址。我们通过 boot/Makefrag 的 -Ttext 0x7C00 来设置链接地址，保证链接器将在生成的代码中生成正确的内存地址。

> 查了一下，BIOS 将引导扇区加载到 0x7C00 这个地址是行业通用的标准

### Exercise 5

> 再次跟踪引导加载程序的前几条指令，找出如果引导加载程序的链接地址错误，第一个会“中断”或执行错误操作的指令。然后将 boot/Makefrag 中的链接地址修改为错误的地址，运行 make clean，用 make 重新编译实验，然后再次跟踪引导加载程序，看看会发生什么。之后别忘了把链接地址改回来，然后再运行 make clean！

这里得明确一下链接地址、加载地址。链接地址是链接器在创建可执行文件时，所假定的程序被加载到内存并开始执行的起始地址。链接器将 .o 文件组合成最终的可执行文件的时候，需要一个起始地址，根据这个起始地址来计算出所有函数和全局变量的绝对地址，链接地址就是这个起始地址，是一个**虚拟地址**。

加载地址是加载器 Loader 把程序从硬盘复制进内存时，所选择的物理内存的起始地址。这是一个**物理地址**。

在有内存管理单元（MMU）的时候，链接地址和加载地址通常是相互独立且不同的，链接器一般会把程序链接到一个标准虚拟地址，比如说 0x400000，而当运行程序时，Linux 内核会找到一块空闲的物理内存，比如说 0x1A2B000，然后把程序加载到这个地方，这里 0x1A2B000 就是加载地址。然后内核会去配置 MMU 的页表，去建立一个映射关系：虚拟地址 0x400000 = 物理地址 0x1A2B000。然后在 CPU 执行指令时，会生成一个逻辑地址（也就是虚拟地址），比如说是 0x400123，MMU 就会根据页表，自动把它翻译成物理地址 0x1A2B123，然后去访问内存。这种情况下，我们可以认为链接地址定义了逻辑地址空间，加载地址是物理地址空间的一部分。

但是刚才那个是在现代 OS 有 MMU 的情况下。Exercise 5 的环境就是个 Boot Loader，这个时候没有 OS，没有 MMU，也就没有啥分页机制的映射关系去翻译地址了。那么，CPU 生成的逻辑地址直接就是物理地址，也因此，程序的**链接地址必须等于它的加载地址**。比如说，BIOS 总是把启动扇区加载到物理内存 0x7C00，那么加载地址 = 0x7C00；为了程序正常工作，我得在 Makefile 里告诉链接器，链接地址 = 0x7C00；链接器生成了一个指令`call 0x7C11`，CPU 执行时就会生成逻辑地址 0x7C11，由于没有 MMU，逻辑地址 0x7C11 就直接作为物理地址，发送到内存总线，命中 BOIS 加载的代码，程序继续执行。

厘清这个关系，我们继续做 Exercise 5。首先要求我们去更改 boot/Makefrag 的链接地址为错误地址。文件的 28 行`$(V)$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o $@.out $^`，把 0x7C00 改成 0x7CCC，在 lab 目录下 make clean 再 make

`obj/boot/boot.asm`里可以看见

![[MIT 6.828 Lab 1] 11](img\[MIT 6.828 Lab 1] 11.png)

这里链接地址已经变成 0x7CCC 了，可以与 Exercise 3 - 1 里面的地址对比一下。

开始调吧，因为 BIOS 加载的地址是不变的，所以还是先断在 0x7C00

![[MIT 6.828 Lab 1] 12](img\[MIT 6.828 Lab 1] 12.png)

这里还是没啥问题，我们接着运行。记得用 si，因为我们把地址改错了，要是用 s n 啥的，它会直接去 boot.out 找符号信息，但是 BIOS 又不会遵循 boot.out 的符号，此时 CPU 的真实 EIP 应该是 0x7C00，然而 gdb 读取的符号又会显示所有的函数啥的都在 0x7CCC 附近，此时就会报错`Cannot find bounds of current function`。

一直走到这里，会发现`ljmp`一直死循环，执行不下去。

![[MIT 6.828 Lab 1] 13](img\[MIT 6.828 Lab 1] 13.png)

这个时候回到执行`make qemu-gdb`的终端，会发现

```sh
make qemu-gdb
sed "s/localhost:1234/localhost:26000/" < .gdbinit.tmpl > .gdbinit
***
*** Now run 'make gdb'.
***
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log  -S
VNC server running on `127.0.0.1:5900'
EAX=00000011 EBX=00000000 ECX=00000000 EDX=00000080
ESI=00000000 EDI=00000000 EBP=00000000 ESP=00006f20
EIP=00007c2d EFL=00000006 [-----P-] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
CS =0000 00000000 0000ffff 00009b00 DPL=0 CS16 [-RA]
SS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
DS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
FS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
GS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
LDT=0000 00000000 0000ffff 00008200 DPL=0 LDT
TR =0000 00000000 0000ffff 00008b00 DPL=0 TSS32-busy
GDT=     007f0001 00000000
IDT=     00000000 000003ff
CR0=00000011 CR2=00000000 CR3=00000000 CR4=00000000
DR0=00000000 DR1=00000000 DR2=00000000 DR3=00000000 
DR6=ffff0ff0 DR7=00000400
EFER=0000000000000000
Triple fault.  Halting for inspection via QEMU monitor.
EAX=00000011 EBX=00000000 ECX=00000000 EDX=00000080
ESI=00000000 EDI=00000000 EBP=00000000 ESP=00006f20
EIP=00007c2d EFL=00000006 [-----P-] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
CS =0000 00000000 0000ffff 00009b00 DPL=0 CS16 [-RA]
SS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
DS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
FS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
GS =0000 00000000 0000ffff 00009300 DPL=0 DS16 [-WA]
LDT=0000 00000000 0000ffff 00008200 DPL=0 LDT
TR =0000 00000000 0000ffff 00008b00 DPL=0 TSS32-busy
GDT=     007f0001 00000000
IDT=     00000000 000003ff
CR0=00000011 CR2=00000000 CR3=00000000 CR4=00000000
DR0=00000000 DR1=00000000 DR2=00000000 DR3=00000000 
DR6=ffff0ff0 DR7=00000400
EFER=0000000000000000
Triple fault.  Halting for inspection via QEMU monitor.
```

首先，`Triple fault. Halting for inspection via QEMU monitor.`，告诉我们 CPU 发生了三重故障，然后 qemu 自己暂停了，并且打印了 CPU 发生故障时的状态。三重故障是 x86 CPU 最严重的错误，不可恢复， CPU 会放弃治疗，直接 Reset。

我们分析一下 CPU 死掉时的状态。EIP = 7C2D，也就是 gdb 里面的指令`[   0:7c2d] => 0x7c2d:  ljmp   $0x8,$0x7cfe`，就是执行这条指令时死掉的。CR0 = 00000011，第 0 位 PE 位是 1，所以已经进了保护模式了。GDT = 007f0001 00000000，IDT = 00000000 000003ff。GDT 的 limit 是 0，也就是说 GDT 表是空的。IDT 的 base 是 0，limit 是 0x3FF，这是 CPU 复位的默认值，boot loader 也根本没有加载好 LDT 表。可以发现执行的指令里面有一条`[   0:7c1e] => 0x7c1e:  lgdtw  0x7d30`，就是把 0x7D30 地址的后 6 个字节的值存进 GDT，从结果来看这个地方就已经开始出错了，之前也提到过这是从 16 位实模式切换到 32 位保护模式的关键步骤。即使CR0 确实是进保护模式了，但是进完保护模式后的第一步 ljmp 死掉了。

可以发现，ljmp 尝试在 GDT 查找索引为 1 的项（选择子 0x8 = 1000b，RPL 是低两位(0, 1位) 00，最高特权级；TI 是第 2 位 0，在 GDT 查找；索引是 1），但是 GDT 的 limit 是 0，根本是个空表，所以查找失败，触发异常；触发异常后得去 IDT 查找异常处理程序，但是 IDT 是 BIOS 留下的实模式的 IDT，现在已经保护模式了，所以这个查找也触发异常了；触发异常后又得去 IDT 查找异常处理程序，跟刚才一样，再次异常。到此为止，三重异常，CPU 直接 Reset 了。

Exercise 5 到此就结束了，别忘了把链接地址改回去，重新 make clean && make

---

除了各个段的信息以外，ELF 中还有一个很重要的字段是`e_entry`，该字段保存的是程序入口点的链接地址：.text 段中程序应该开始执行的内存地址。用下面的命令可以看见入口点

```sh
objdump -f obj/kern/kernel
```

![[MIT 6.828 Lab 1] 14](img\[MIT 6.828 Lab 1] 14.png)

跟我们在 Exercise 3 - 2 的一个小问的答案是一样的

### Exercise 6

> 我们可以使用 GDB 的 x 命令检查内存。GDB 手册中有详细的说明，但目前只需知道 x/Nx ADDR 命令会在 ADDR 处打印 N 个字的内存即可。（请注意，命令中的两个“x”均为小写。）警告：字的大小并非通用标准。在 GNU 汇编语言中，一个字占两个字节（xorw 中的“w”代表“字”，即 2 个字节）。
>
> 重启机器（退出 QEMU/GDB 并重新启动）。检查 BIOS 进入引导加载程序时位于 0x00100000 处的 8 个字内存，然后再次检查引导加载程序进入内核时位于 0x00100000 处的 8 个字内存。为什么它们不同？第二个断点处是什么？（实际上不需要使用 QEMU 来回答这个问题。只需思考即可。）

在进入 Boot Loader 时，0x00100000 处的 8 个字的内存如下

![[MIT 6.828 Lab 1] 15](img\[MIT 6.828 Lab 1] 15.png)

全是 0，这个时候因为内核程序还没加载进内存。在 Exercise 3 里面也提到过，得进了 bootmain 函数才会加载进内存。

![[MIT 6.828 Lab 1] 16](img\[MIT 6.828 Lab 1] 16.png)

然后进入内核后的第一条指令处下断点，可以看见现在的内存如上。这里就是 bootmain 函数已经把内核加载进内存了，这些值就是加载的内容。

# Part 3: The Kernel

现在开始就会更详细地讨论 JOS 内核了。内核首先会编写一些汇编代码，用于进行设置，以便 C 代码能够正确执行。

## Using virtual memory to work around position dependence

在运行 bootloader 时，链接地址和加载地址是一样的。但是进入内核后，这两者就不一样了。在`kern/kernel.ld`的 SECTIONS 里能看见链接地址和加载地址。操作系统内核通常会链接并运行在一个非常高的虚拟地址内上，例如 0xF0100000，目的是把低地址留给用户使用。

一般的机器没有这么多物理内存，所以得使用 CPU 的内存管理单元去把虚拟地址 0xF0100000 映射到物理地址 0x00100000 上。在 Exercise 5 里面我们简单讲过，这是通过分段分页机制实现的。

现在我们只映射前 4 MB 内存，这足以让我们启动并运行。我们使用手写、静态初始化的页目录和页表 `kern/entrypgdir.c`，目前无需了解其工作原理的细节，只需了解其实现的效果即可。

这里会涉及一个重要标志：`CR0_PG`，CR0 寄存器的第 31 位是 PG 位，当该位被设置时（为 1 时），启用分页机制；当该位被清除时（为 0 时），禁用分页机制。当分页被禁用时，所有线性地址都被视为物理地址，也就是说没有分页机制，通过段基址加偏移后就是真实物理地址。如果 PE 标志（寄存器 CR0 的第 0 位）未设置，则 PG 标志没有影响；在 PE 标志被清除时设置 PG 标志会导致通用保护异常（#GP）。

### Exercise 7

> 使用 QEMU 和 GDB 跟踪 JOS 内核 并在 `movl %eax, %cr0` 处停止。检查 0x00100000 和 0xf0100000 处的内存。现在，使用 stepi GDB 命令单步跳过该指令。再次检查 0x00100000 和 0xf0100000 处的内存。确保你理解刚刚发生了什么。
>
> 新的映射建立*后，* 如果映射不存在，第一条无法正常工作的指令是什么？注释掉 `kern/entry.S` 中的 `movl %eax, %cr0` 指令，跟踪它，看看你是否正确。

在 `mov %eax, %cr0`处

![[MIT 6.828 Lab 1] 17](img\[MIT 6.828 Lab 1] 17.png)

而当我们步过这个指令后

![[MIT 6.828 Lab 1] 18](img\[MIT 6.828 Lab 1] 18.png)

明显 0xF0100000 的内容改变了，而且与 0x00100000 处的值完全一致。这时已经完成了映射。

```assembly
(gdb) si
=> 0x100015:    mov    $0x112000,%eax
0x00100015 in ?? ()
(gdb) 
=> 0x10001a:    mov    %eax,%cr3
0x0010001a in ?? ()
(gdb) 
=> 0x10001d:    mov    %cr0,%eax
0x0010001d in ?? ()
(gdb) 
=> 0x100020:    or     $0x80010001,%eax
0x00100020 in ?? ()
(gdb) 
=> 0x100025:    mov    %eax,%cr0
0x00100025 in ?? ()
(gdb) x/8x 0x00100000
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x2000b812      0x220f0011      0xc0200fd8
(gdb) x/8x 0xf0100000
0xf0100000 <_start+4026531828>: 0x00000000      0x00000000      0x00000000      0x00000000
0xf0100010 <entry+4>:   0x00000000      0x00000000      0x00000000      0x00000000
(gdb) si
=> 0x100028:    mov    $0xf010002f,%eax
0x00100028 in ?? ()
(gdb) x/8x 0x00100000
0x100000:       0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0x100010:       0x34000004      0x2000b812      0x220f0011      0xc0200fd8
(gdb) x/8x 0xf0100000
0xf0100000 <_start+4026531828>: 0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0xf0100010 <entry+4>:   0x34000004      0x2000b812      0x220f0011      0xc0200fd8
```

这个过程中的汇编如上。简单讲一下这里发生了什么。首先得知道 CR3 寄存器是用于存放页目录表物理内存基地址的。`mov $0x112000,%eax` `mov %eax,%cr3`完成了存放。`or $0x80010001,%eax`，0x80010001 这个数二进制的第 31 位是 1，`mov %eax,%cr0`就确保了 CR0 寄存器的 PG 位为 1。此时分页机制开启，内存管理单元启用，从此刻开始，CPU 发出的所有内存地址都不再被当作物理地址，而是被当作虚拟地址，并且必须经过 CR3 寄存器指向的页表的翻译，才能转换成物理地址去访问内存。

也就是说，`mov %eax, %cr0`前，CPU 是把 0x100000 和 0xF0100000 都当做物理地址，显然这个高地址我们的 JOS 根本够不到，这个地址根本不存在，所以里面是全 0；mov 之后，分页机制开启，CPU 把 0x100000 和 0xF0100000 都当做虚拟地址。Exercise 7 上面有一段话

> 一旦设置了 `CR0_PG` ，内存引用就是虚拟地址，虚拟内存硬件会将其转换为物理地址。entry_pgdir 会将 0xf0000000 到 0xf0400000 范围内的虚拟地址转换为 0x00000000 到 0x00400000 范围内的物理地址，以及将 0x00000000 到 0x00400000 范围内的虚拟地址 `entry_pgdir` 为 0x00000000 到 0x00400000 范围内的物理地址。

也就是说，CPU 会把 0x100000 和 0xF0100000 这两个虚拟地址都映射到物理地址 0x100000，所以我们会看见此时两个地址的值都是一样的。

显然，如果我们把开启分页机制的这句话注释掉，下一个访问 0xF0100000 附近的地址的指令会访问的高地址不存在，直接触发异常。

现在来试一下

![[MIT 6.828 Lab 1] 19](img\[MIT 6.828 Lab 1] 19.png)

尝试 jmp 这个高地址时，gdb 直接提示 Remote connection closed 了。

![[MIT 6.828 Lab 1] 20](img\[MIT 6.828 Lab 1] 20.png)

可以看见这边提示 qemu: fatal: Trying to execute code outside RAM or ROM at 0xf010002c，地址越界，qemu 直接 core dumped 了。

## Formatted Printing to the Console

现在我们要在操作系统内核实现自己的 I/O 操作。仔细阅读 `kern/printf.c` 、 `lib/printfmt.c` 和 `kern/console.c` ，并确保理解它们之间的关系。

### Exercise 8

> 我们省略了一小段代码——使用“%o”形式的模式打印八进制数所需的代码。请查找并填写此代码片段。

这个省略的代码在 `lib/printfmt.c`

```cpp
// (unsigned) octal
		case 'o':
			// Replace this with your code.
			putch('X', putdat);
			putch('X', putdat);
			putch('X', putdat);
			break;
```

在 kern/init.c 中使用了这个 %o 的格式化输出

```cpp
void
i386_init(void)
{
	extern char edata[], end[];

	// Before doing anything else, complete the ELF loading process.
	// Clear the uninitialized global data (BSS) section of our program.
	// This ensures that all static/global variables start out zero.
	memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	cons_init();

	cprintf("6828 decimal is %o octal!\n", 6828);

	// Test the stack backtrace function (lab 1 only)
	test_backtrace(5);

	// Drop into the kernel monitor.
	while (1)
		monitor(NULL);
}
```

在 Simulating the x86 这一节编译 qemu 的时候也能看见这个输出是不对劲的，这里我们在 lab 下 `make qemu`一下

```sh
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log 
VNC server running on `127.0.0.1:5900'
6828 decimal is XXX octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

可以看见这里输出的是 6828 decimal is XXX octal!，因为 case 'o' 的时候只 putch 了仨 X

现在我们就仿照其它的格式化输出，比如 unsigned decimal，补一下代码即可

```cpp
// (unsigned) octal
		case 'o':
			// Replace this with your code.
			// putch('X', putdat);
			// putch('X', putdat);
			// putch('X', putdat);
			// break;
			num = getuint(&ap, lflag);
			base = 8;
			goto number;
```

然后重新 make qemu

```sh
+ cc lib/printfmt.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ mk obj/kern/kernel.img
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log 
VNC server running on `127.0.0.1:5900'
6828 decimal is 15254 octal!
entering test_backtrace 5
entering test_backtrace 4
entering test_backtrace 3
entering test_backtrace 2
entering test_backtrace 1
entering test_backtrace 0
leaving test_backtrace 0
leaving test_backtrace 1
leaving test_backtrace 2
leaving test_backtrace 3
leaving test_backtrace 4
leaving test_backtrace 5
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> QEMU: Terminated
```

可以看见，现在这个 octal 正确输出了，10 进制 6828 也确实是 8 进制 15254，改的没问题。

> 1. 解释 printf.c 和 console.c 之间的接口。具体来说，console.c 导出了什么函数？printf.c 如何使用这个函数？

现在就得看之前提到的 `kern/printf.c` 、 `lib/printfmt.c` 和 `kern/console.c` 这仨文件了。

printf.c 调用了 console.c 中的 `cputchar` 函数，并将其封装在 `putch` 函数中。在`vcprintf`中还将`putch`传参给`vprintfmt`，这个函数是 printfmt.c 的。

> 2. 解释 `console.c` 中的以下内容：
>
>    ```cpp
>    1      if (crt_pos >= CRT_SIZE) {
>    2              int i;
>    3              memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
>    4              for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
>    5                      crt_buf[i] = 0x0700 | ' ';
>    6              crt_pos -= CRT_COLS;
>    7      }
>    ```

作用是丢弃屏幕顶端的一行，将所有内容向上移动一行，然后在屏幕底部留出新的空白行。

首先第一行里的`crt_pos`是当前光标在屏幕上的位置，`CRT_SIZE`是整个屏幕的大小，这就是在说光标是否已经移动到了屏幕的末尾或超出了屏幕，如果为 true，则屏幕已经写满了，得新开一行。

第三行，`crt_buf`指向显存，`CRT_COLS` 是屏幕的列数，`memmove`进行内存拷贝。也就是，把从屏幕第二行开始的所有内容(`crt_buf + CRT_COLS`)向前移动一行，覆盖掉原来第一行(`crt_buf`)

第四行第五行就是在清空最后一行，填充为空格。第六行调整光标位置，也往上移一行，指向现在的空白行。

> 3. 对于以下问题，你可能需要参考第二讲的笔记。这些笔记涵盖了 GCC 在 x86 上的调用约定。
>
>    逐步跟踪以下代码的执行过程：
>
>    ```cpp
>    int x = 1, y = 3, z = 4;
>    cprintf("x %d, y %x, z %d\n", x, y, z);
>    ```
>
>    在调用 cprintf() 时，fmt 指向什么？ap 指向什么？
>
>    按执行顺序列出对 cons_putc、va_arg 和 vcprintf 的每次调用。对于 cons_putc，也列出其参数。对于 va_arg，列出调用前后 ap 指向的内容。对于 vcprintf，列出其两个参数的值。

一个想法是仿照 %o 的运行，我们直接在 i386_init 打印 octal 那段话的附近把这个代码写进去，这样就方便我们去看见它的输出以及在原生的 jos 环境中去调试它。

obj/kern/kernel.asm 中

```assembly
	// To Debug Lab 1 Exercise 8 Q3.
	cprintf("To Debug Lab 1 Exercise 8 Q3.\n");
f01000e8:	8d 83 d0 07 ff ff    	lea    -0xf830(%ebx),%eax
f01000ee:	89 04 24             	mov    %eax,(%esp)
f01000f1:	e8 71 09 00 00       	call   f0100a67 <cprintf>
	int x = 1, y = 3, z = 4;
	cprintf("x %d, y %x, z %d\n", x, y, z);
f01000f6:	6a 04                	push   $0x4
f01000f8:	6a 03                	push   $0x3
f01000fa:	6a 01                	push   $0x1
f01000fc:	8d 83 8a 07 ff ff    	lea    -0xf876(%ebx),%eax
f0100102:	50                   	push   %eax
f0100103:	e8 5f 09 00 00       	call   f0100a67 <cprintf>
```

断在 f01000f6

![[MIT 6.828 Lab 1] 21](img\[MIT 6.828 Lab 1] 21.png)

调试过程如上，跟进 cprintf 后，可以看见传给 vcprintf 的俩参数 fmt 指向的是 cprintf 的格式化字符串的内容，ap 指向的是常量 x 的值，后面是 y z 的值。再后面是我们上一个 cprintf 打印的调试信息。

这里还能看一个有意思的点

![[MIT 6.828 Lab 1] 22](img\[MIT 6.828 Lab 1] 22.png)

也是在 kern/kernel.asm 中，cprintf 的汇编，va_start 只有可变参数列表的起始地址，va_list ap 连栈上地址都没有被分配，直接拿 eax 去传递结果了；va_end 甚至连一行汇编都没有了。这里我怀疑就是因为 va_start 的参数 ap 没地址，va_end 对应地也没东西可清理了，所以直接没汇编了。i386 的编译器优化真神奇。是不是说明理论上 i386 平台下甚至不需要写 va_end，这符合标准吗(x

继续下个小问题，va_arg 在 vprintfmt 内调用，const_putc 在 putch 内调用，这两个都是在 vcprintf 过程中被调用的。

大概过程就是

```cpp
vcprintf (fmt=0xf0101a92 "x %d, y %x, z %d\n", ap=0xf010ffd4 "\001")

consputc (c=120) // x
consputc (c=32)  // ' '
va_arg(&ap, int) // 调用前 ap=0xf010ffd4，调用后 ap=0xf010ffd8
consputc (c=49) // 1
consputc (c=44) // ,
consputc (c=32) // ' '

consputc (c=121) // y
consputc (c=32) // ' '
va_arg(&ap, int) // 调用前 ap=0xf010ffd8，调用后 ap=0xf010ffdc
consputc (c=51) // 3
consputc (c=44) // ,
consputc (c=32) // ' '

consputc (c=122) // z
consputc (c=32) // ' '
va_arg(&ap, int) // 调用前 ap=0xf010ffdc，调用后 ap=0xf010ffe0
consputc (c=52) // 4
consputc (c=10) // \n
```

> 4. 运行以下代码。
>
>    ```cpp
>        unsigned int i = 0x00646c72;
>        cprintf("H%x Wo%s", 57616, &i);
>    ```
>
> 输出是什么？解释一下如何按照上一个练习的步骤得出这个输出。
>
> 输出取决于 x86 是小端字节序。如果 x86 是大端字节序，你会将 i 设置为多少才能得到相同的输出？你需要将 57616 改为其他值吗？

依然是在 i386_init 这里打印出来看看。为了美观，我写的是`cprintf("H%x Wo%s\n", 57616, &i);`，多一个换行符

![[MIT 6.828 Lab 1] 23](img\[MIT 6.828 Lab 1] 23.png)

可以看见打印的是 "He110 World"

这个也很简单，57616 被格式化字符串认为是 16 进制的数，所以 57616 = 0xe110，用的是 x，所以是小写，那么 H%x 就是 He110。0x00646c72，0x00, 0x64, 0x6c, 0x72，我们这里是 i386 环境，是小端序，所以内存中的顺序是 0x72, 0x6c, 0x64, 0x00，翻译为 ASCII 就是 rld\0。总体来看就是 He110 World\0。

如果是大端序，我们把 i 改为 0x726C6400 就行，57616 不需要改。手上暂时没有大端序环境，验证起来不方便，就不验了。

> 5. 在下面的代码中，'y=' 后面会打印什么？（注意：答案不是一个具体的值。）为什么会发生这种情况？
>
> ```cpp
> cprintf("x=%d y=%d", 3);
> ```

![[MIT 6.828 Lab 1] 24](img\[MIT 6.828 Lab 1] 24.png)

依然是在 i386_init 里改。这里可以看见，第二个 %d 打印栈上第一个 %d 后面的内存。由于是 %d，所以认为是一个十进制整型，0x640 = 1600，所以打印的是 y=1600。这也是格式化字符串漏洞最基础的一个原理。

> 6. 假设 GCC 改变了它的调用约定，将参数按声明顺序压入栈中，即最后一个参数最后压入栈。你需要如何修改 cprintf 或其接口，才能让它仍然能够传递可变数量的参数？

很开放的问题。可以参考 stdcall 调用约定的方法，这个时候我们可以认为要去修改 va_start 宏。它现在应该是在 fmt 的高地址处去寻找参数，然后依然由调用者去清理堆栈，因为函数调用者是知道可变参数具体有几个的。

还有一个想法就是要新增一个传参，写明可变参数到底有几个，但是这不优雅，而且一旦出错就会整个崩溃。

## The Stack

在 Lab 1的最后一个练习中，我们将更详细地探讨 C 语言在 x86 上使用堆栈的方式，并在此过程中编写一个有用的新内核监视器函数，该函数打印堆栈的回溯：从导致当前执行点的嵌套调用指令保存的指令指针 (IP) 值的列表。

### Exercise 9

> 确定内核初始化堆栈的位置，以及堆栈在内存中的确切位置。内核如何为其堆栈保留空间？堆栈指针初始化后指向该保留区域的哪个“末端”？

之前分析的时候提到过，bootmain() 执行后就是 entry.S 和 init.c，bootmain() 内没有堆栈操作。

而 init.c 中已经开始做很多正常的涉及堆栈的操作了，也不是初始化，也就是说在进入 init.c 之前，堆栈应该已经初始化完毕了。

所以，应该是这两行：

```cpp
relocated:
	# Clear the frame pointer register (EBP)
	# so that once we get into debugging C code,
	# stack backtraces will be terminated properly.
	movl	$0x0,%ebp			# nuke frame pointer

	# Set the stack pointer
	movl	$(bootstacktop),%esp
```

那么在内存的什么地方呢？

```sh
$ objdump -t obj/kern/kernel | grep bootstacktop
f0110000 g       .data  00000000 bootstacktop
$ objdump -D obj/kern/kernel | grep f0110000
f0100034:       bc 00 00 11 f0          mov    $0xf0110000,%esp
f0110000 <entry_pgtable>:
f0110000:       03 00                   add    (%eax),%eax
```

那么，栈顶是在 0xF0110000（其实该叫栈底？毕竟是向下生长，大概是门字型这样子）

entry.S 中

```assembly
.data
###################################################################
# boot stack
###################################################################
	.p2align	PGSHIFT		# force page alignment
	.globl		bootstack
bootstack:
	.space		KSTKSIZE
	.globl		bootstacktop   
bootstacktop:
```

可以看见，是在 .data 段中预留了一些空间作为栈。

同时

```cpp
mmu.h:
#define PGSIZE		4096		// bytes mapped by a page
memlayout.h
#define KSTKSIZE	(8*PGSIZE)   		// size of a kernel stack
```

栈大小 0x8000 个字节，所以栈在内存中的 0xF0108000 - 0xF0110000

当然，如果想要的是物理地址而不是虚拟地址，那就是 0x00108000 - 0x00110000，这个映射关系之前解释过了。

相应地，堆栈指针，也就是 esp，当然是指向 0xF0110000，也就是 bootstacktop

---

x86 堆栈指针（esp 寄存器）指向当前正在使用的堆栈的最低位置。低于该位置的堆栈保留区域中的所有内容都是空闲的。将值压入堆栈的操作涉及减少堆栈指针的值，然后将值写入堆栈指针指向的位置。从堆栈中弹出值的操作涉及读取堆栈指针指向的值，然后增加堆栈指针的值。在 32 位模式下，堆栈只能保存 32 位值，并且 esp 始终能被 4 整除。各种 x86 指令（如 call）都“硬连接”使用堆栈指针寄存器。

相比之下，ebp（基指针）寄存器主要按照软件约定与堆栈关联。在进入 C 函数时，函数的序言代码通常会将前一个函数的基指针压入堆栈进行保存，然后将当前 esp 值复制到 ebp 中，直至函数执行完毕。如果程序中的所有函数都遵循此约定，那么在程序执行的任何时刻，都可以通过跟踪保存的 ebp 指针链，并准确确定是哪个嵌套的函数调用序列导致了程序到达此特定点，从而回溯堆栈。此功能尤其有用，例如，当某个函数由于传递了错误的参数而导致断言失败或崩溃，但你不确定是谁传递了错误的参数时。堆栈回溯可以帮助你找到有问题的函数。

### Exercise 10

> 为了熟悉 x86 上的 C 调用约定，请在 obj/kern/kernel.asm 中找到 test_backtrace 函数的地址，在那里设置一个断点，并检查内核启动后每次调用该函数时会发生什么。test_backtrace 函数的每一层递归嵌套会在堆栈上压入多少个 32 位字？这些字是什么？
>
> 请注意，为了使本练习能够正常运行，你应该使用工具页面或 Athena 上提供的 QEMU 补丁版本。否则，你必须手动将所有断点和内存地址转换为线性地址。

首先，test_backtrace() 源码是

```cpp
// Test the stack backtrace function (lab 1 only)
void
test_backtrace(int x)
{
	cprintf("entering test_backtrace %d\n", x);
	if (x > 0)
		test_backtrace(x-1);
	else
		mon_backtrace(0, 0, 0);
	cprintf("leaving test_backtrace %d\n", x);
}
```

init.c 里传参是 5。

```assembly
=> 0xf0100100 <i386_init+90>:   call   0xf0100040 <test_backtrace>

Breakpoint 1, 0xf0100100 in i386_init () at kern/init.c:51
51              test_backtrace(5);
(gdb) info r
eax            0x0      0
ecx            0x3d4    980
edx            0x3d5    981
ebx            0xf0111308       -267316472
esp            0xf010ffe0       0xf010ffe0
ebp            0xf010fff8       0xf010fff8
```

断在 call backtrace 处，此时 esp 是 0xf010ffe0

然后去断在 call mon_backtrace 处

```asm
(gdb) b *0xf0100073
Breakpoint 2 at 0xf0100073: file kern/init.c, line 18.
(gdb) c
Continuing.
=> 0xf0100073 <test_backtrace+51>:      call   0xf0100894 <mon_backtrace>

Breakpoint 2, 0xf0100073 in test_backtrace (x=0) at kern/init.c:18
18                      mon_backtrace(0, 0, 0);
(gdb) x/80x $esp
0xf010ff20:     0x00000000      0x00000000      0x00000000      0xf010004a
0xf010ff30:     0xf0111308      0x00000001      0xf010ff58      0xf01000a1
0xf010ff40:     0x00000000      0x00000001      0xf010ff78      0xf010004a
0xf010ff50:     0xf0111308      0x00000002      0xf010ff78      0xf01000a1
0xf010ff60:     0x00000001      0x00000002      0xf010ff98      0xf010004a
0xf010ff70:     0xf0111308      0x00000003      0xf010ff98      0xf01000a1
0xf010ff80:     0x00000002      0x00000003      0xf010ffb8      0xf010004a
0xf010ff90:     0xf0111308      0x00000004      0xf010ffb8      0xf01000a1
0xf010ffa0:     0x00000003      0x00000004      0x00000000      0xf010004a
0xf010ffb0:     0xf0111308      0x00000005      0xf010ffd8      0xf01000a1
0xf010ffc0:     0x00000004      0x00000005      0x00000000      0xf010004a
0xf010ffd0:     0xf0111308      0x00010094      0xf010fff8      0xf0100105
0xf010ffe0:     0x00000005      0x00000003      0x00000640      0x00000000
0xf010fff0:     0x00000000      0x00010094      0x00000000      0xf010003e
...
```

这个 0 1 2 3 4 5 显然是 test_backtrace 的传参，0xf010004a 是递归调用中 test_backtrace 的函数返回地址，0xf0100105 则是 test_backtrace(5) 的返回地址

顺便，倒数第二行的 0x5 的地址是 0xf010ffe0，是第一次 call backtrace 时的 esp 栈指针，所以这个是第一次调用时存的参数。

每一层递归嵌套会在堆栈上压入多少个 32 位字呢？

* call test_backtrace 时压入返回地址，4 字节
* push %ebp，4 字节
* push %esi，4 字节
* push %ebx，4 字节
* sub $0x8, %esp，8 字节
* push %esi，4 字节
* push %eax，4 字节

一共 32 个字节，也就是 8 个 32 位字(int)，也就是上面 x/80x $esp 每两行是一个栈帧。

---

上面的练习应该能帮你掌握实现堆栈回溯函数所需的信息，其名为 mon_backtrace()。该函数的原型已经在 kern/monitor.c 中准备好了，可以完全用 C 语言实现，但你可能会发现 inc/x86.h 中的 read_ebp() 函数很有用。你还需要将这个新函数挂接到内核监视器的命令列表中，以便用户可以交互地调用它。

回溯函数应以以下格式显示函数调用帧的列表：

```asm
Stack backtrace:
  ebp f0109e58  eip f0100a62  args 00000001 f0109e80 f0109e98 f0100ed2 00000031
  ebp f0109ed8  eip f01000d6  args 00000000 00000000 f0100058 f0109f28 00000061
```

每行包含一个 ebp、eip 和 args。ebp 值表示该函数使用的栈的基指针：即函数进入后栈指针的位置，函数序言代码设置了基指针。列出的 eip 值是函数的返回指令指针：函数返回时控制权将返回到的指令地址。返回指令指针通常指向调用指令之后的指令（为什么？）。最后，args 后面列出的五个十六进制值是该函数的前五个参数，它们会在函数调用之前被压入栈中。当然，如果函数调用时使用的参数少于五个，那么这五个值并非全部有用。（为什么回溯代码无法检测出实际的参数数量？如何修复这个限制？）

打印的第一行反映了当前正在执行的函数，即 mon_backtrace 本身；第二行反映了调用 mon_backtrace 的函数；第三行反映了调用该函数的函数，依此类推。你应该打印所有未完成的堆栈帧。通过研究 kern/entry.S，你会发现有一种简单的方法可以判断何时停止。

以下是你在 K&R 第 5 章中读到的几个具体要点，值得在接下来的练习和未来的实验中记住。

* 如果 int \*p = (int*)100，那么 (int)p + 1 和 (int)(p + 1) 是不同的数字：第一个是 101，但第二个是 104。当将整数添加到指针时，如第二种情况，整数会隐式乘以指针指向的对象的大小。
* p[i] 的定义与 *(p+i) 相同，指的是 p 指向的内存中的第 i 个对象。上述加法规则有助于在对象大于一个字节时使此定义有效。
* &p[i] 与 (p+i) 相同，得出 p 指向的内存中第 i 个对象的地址。

虽然大多数 C 程序不需要在指针和整数之间进行类型转换，但操作系统经常需要这样做。每当你看到涉及内存地址的加法时，都要问问自己，这是一个整数加法还是指针加法，并确保被加的值是否被正确乘以。

### Exercise 11

> 按照上面指定的方式实现 backtrace 函数。使用与示例相同的格式，否则评分脚本会出错。当你认为它运行正确时，运行 make grade 来查看其输出是否符合评分脚本的预期，如果不符合，请修复。提交实验 1 的代码后，你可以随意更改 backtrace 函数的输出格式。
>
> 如果你使用 read_ebp()，请注意，GCC 可能会生成“优化”代码，在 mon_backtrace() 的函数序言之前调用 read_ebp()，这会导致堆栈跟踪不完整（缺少最近一次函数调用的堆栈帧）。虽然我们已尝试禁用导致这种重新排序的优化，但你可能需要检查 mon_backtrace() 的汇编代码，并确保对 read_ebp() 的调用发生在函数序言之后。

很显然，通过 ebp 建立 caller 和 callee 的关系，用 ebp 一直这样向高地址去找。kern/entry.S 中

```asm
movl	$0x0,%ebp
movl	$(bootstacktop),%esp
call	i386_init
```

所以，一直找到 ebp = 0 就可以停下来了。

```asm
       |-------------------|
...    | 参数 N            |  <-- 高地址
%ebp+12| 参数 2 (p+3)      |
%ebp+8 | 参数 1 (p+2)      |
...    |-------------------|  <-- 调用者的栈帧
%ebp+4 | 返回地址 (Saved EIP, p+1) |
...    |-------------------|
%ebp   | 旧的 %ebp (Saved EBP, *p) | <-- 当前函数的 %ebp
...    |-------------------|  <-- 当前函数的栈帧
%ebp-4 | 局部变量           |
...    |-------------------|  <-- 低地址
```

所以

```cpp
	uint32_t ebp = read_ebp();
	uint32_t *p = (uint32_t *)ebp;
	cprintf("Stack backtrace:\n");

	while (ebp != 0) {
		p = (uint32_t *) ebp;
		cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, *(p + 1), *(p + 2), *(p + 3), *(p + 4), *(p + 5), *(p + 6));
		ebp = *p;
	}
```

即可。

read_ebp() 在 inc/x86.h 中声明

```cpp
static inline uint32_t
read_ebp(void)
{
	uint32_t ebp;
	asm volatile("movl %%ebp,%0" : "=r" (ebp));
	return ebp;
}
```

结果如下

![[MIT 6.828 Lab 1] 25](img\[MIT 6.828 Lab 1] 25.png)

现在回答一下描述中的两个问题

> 返回指令指针通常指向调用指令之后的指令（为什么？）

函数调用指令之后的指令会充当函数的返回地址，函数返回后，CPU 就该接着调用指令之后的指令继续执行了。

> 当然，如果函数调用时使用的参数少于五个，那么这五个值并非全部有用。（为什么回溯代码无法检测出实际的参数数量？如何修复这个限制？）

因为函数的参数数量这种信息在编译后就丢掉了，运行时也不会在 CPU 或者内存中出现。其实在编译的时候用 -g，加上调试信息就行，这样的话函数的参数数量就能在符号表里找到。

---

此时，你的回溯函数应该会返回栈上导致 mon_backtrace() 执行的函数调用者的地址。然而，在实际操作中，你通常需要知道这些地址对应的函数名。例如，你可能想知道哪些函数可能包含导致内核崩溃的错误。

为了帮助你实现此功能，我们提供了函数 debuginfo_eip()，它在符号表中查找 eip 并返回该地址的调试信息。此函数定义在 kern/kdebug.c 中。

### Exercise 12

> 修改你的堆栈回溯函数，使其显示每个 eip 对应的函数名、源文件名和行号。
>
> 在 debuginfo_eip 中，\_\_STAB_* 是从哪里来的？这个问题的答案很长；为了帮助你找到答案，以下是一些你可能需要做的事情：
>
> * 在文件 kern/kernel.ld 中查找 \_\_STAB_*
> * 运行 objdump -h obj/kern/kernel
> * 运行 objdump -G obj/kern/kernel
> * 运行 gcc -pipe -nostdinc -O2 -fno-builtin -I. -MD -Wall -Wno-format -DJOS_KERNEL -gstabs -c -S kern/init.c，并查看 init.s。
> * 检查引导加载程序是否在加载内核二进制文件时将符号表加载到内存中。
>
> 通过插入对 stab_binsearch 的调用来查找地址的行号，完成 debuginfo_eip 的实现。
>
> 向内核监视器添加回溯命令，并扩展 mon_backtrace 的实现，使其调用 debuginfo_eip 并为每个堆栈帧打印一行，格式如下：
>
> ```asm
> K> backtrace
> Stack backtrace：
> ebp f010ff78 eip f01008ae args 00000001 f010ff8c 00000000 f0110580 00000000
> kern/monitor.c:143: monitor+106
> ebp f010ffd8 eip f0100193 args 00000000 00001aac 00000660 00000000 00000000
> kern/init.c:49: i386_init+59
> ebp f010fff8 eip f010003d args 00000000 00000000 0000ffff 10cf9a00 0000ffff
> kern/entry.S:70: <unknown>+0
> K>
> ```
>
> 每行给出文件名以及堆栈帧 eip 在该文件中的行号，后跟函数名以及 eip 相对于函数第一条指令的偏移量（例如，monitor+106 表示返回 eip 位于 monitor 开头之后 106 个字节处）。
>
> 请务必将文件名和函数名单独打印，以免与评分脚本混淆。
>
> 提示：printf 格式字符串提供了一种简单（尽管不太明显）的方法来打印非空结尾的字符串，例如 STABS 表中的字符串。printf("%.*s", length, string) 最多打印 string 中的 length 个字符。查看 printf 手册页，了解其工作原理。
>
> 您可能会发现回溯中缺少某些函数。例如，您可能会看到对 monitor() 的调用，但没有看到对 runcmd() 的调用。这是因为编译器内联了一些函数调用。其他优化可能会导致您看到意外的行号。如果您从 GNUMakefile 中删除 -O2 选项，回溯可能会更有意义（但您的内核运行速度会更慢）。
>

还算比较简单。

首先是补全 debugger_eip()，现在的 kdebug.c 其实已经写好了一大半了，就是缺了个查找行号的功能。根据提示，我们需要代表行号的 STABS 类型，然后去调用 stab_binsearch()。inc/stab.h 明确地告诉我们：`#define N_SLINE   0x44   // text segment line number`

所以

```cpp
stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
if (lline <= rline) {
    info->eip_line = stabs[lline].n_desc;
}
```

看一眼 stab_binsearch() 的实现就知道它是在做二分，在 [lline, rline] 中去二分类型是 N_SLINE 的，最接近且不大于 addr 的 STAB 地址。

如果找到了，行号就存放在 n_desc 字段中，把它赋值给 info -> eip_line。

```cpp
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
```

mon_backtrace() 中整体和之前一样，加入了找 eip 的部分。声明 Eipdebuginfo 类型的结构体，这个类型在 kern/kdebug.h 中有声明。调用 debuginfo_eip() 去获取调试信息，然后打印就行。这个`printf("%.*s", length, string)`还挺 nb 的，学到了。

![[MIT 6.828 Lab 1] 26](img\[MIT 6.828 Lab 1] 26.png)

最后回答一下 \_\_STAB_* 来自哪里。简单来讲，这是链接器在链接内核时放置的，用来标记调试信息在内存中的起始和结束位置。

当使用 `-gstabs` 编译时，编译器会生成机器码，同时也会生成 STABS 格式的调试信息，并将其放入生成的 .o 目标文件的 .stab 和 .stabstr 中。

我们在 kern/kernel.ld 能看见

```cpp
/* Include debugging information in kernel memory */
.stab : {
    PROVIDE(__STAB_BEGIN__ = .);
    *(.stab);
    PROVIDE(__STAB_END__ = .);
    BYTE(0)		/* Force the linker to allocate space
				   for this section */
}

.stabstr : {
    PROVIDE(__STABSTR_BEGIN__ = .);
    *(.stabstr);
    PROVIDE(__STABSTR_END__ = .);
    BYTE(0)		/* Force the linker to allocate space
				   for this section */
}
```

这就是在告诉编译器，得把所有输入文件中的 .stab 节合并在一起，在这个合并块最开始的地方创建一个名为 `__STAB_BEGIN__` 的全局符号，它的值就是当前的地址。在这个块的末尾创建一个名为 `__STAB_END__` 的全局符号，它的值就是结束的地址。.stabstr 也是一样。当 bootloader 加载内核时，它会根据程序头信息将这些 STABS 调试信息在内的整个段加载到物理内存中。所以当内核运行时，debugger_eip() 就能通过它们来访问这片包含所有调试信息的内存区域。

Lab 1 到此结束。

