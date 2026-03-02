# Boot xv6
clone & make 即可
```sh
git clone https://github.com/mit-pdos/xv6-public.git
cd xv6-public
make
```

# Finding and breaking at an address

首先找到内核入口点 `_start` 地址

```sh
iplayforsg@ubuntu:~/Desktop/MIT_6.828/xv6-public$ nm kernel | grep _start
8010a48c D _binary_entryother_start
8010a460 D _binary_initcode_start
0010000c T _start
iplayforsg@ubuntu:~/Desktop/MIT_6.828/xv6-public$ 
```

0x0010000c，跟 jos 一样

然后调试，在 xv6-public 里 `make qemu-gdb`，再新开一个终端，进入同一个目录然后`gdb`，`br *0x0010000c` 下断点，然后 continue

# Exercise: What is on the stack?

> 在上述断点处停止后，查看寄存器和堆栈内容：
>
> ```
> (gdb) info reg
> ...
> (gdb) x/24x $esp
> ...
> (gdb)
> ```
>
> 在栈上每个非零值旁边写一段简短的注释（3-5个字），解释它的含义。栈输出的哪一部分才是真正的栈？（提示：并非全部。）

```sh
(gdb) info reg
eax            0x0      0
ecx            0x0      0
edx            0x1f0    496
ebx            0x10094  65684
esp            0x7bdc   0x7bdc
ebp            0x7bf8   0x7bf8
esi            0x10094  65684
edi            0x0      0
eip            0x10000c 0x10000c
eflags         0x46     [ PF ZF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x0      0
gs             0x0      0
(gdb) x/24x $esp
0x7bdc: 0x00007d8d      0x00000000      0x00000000      0x00000000
0x7bec: 0x00000000      0x00000000      0x00000000      0x00000000
0x7bfc: 0x00007c4d      0x8ec031fa      0x8ec08ed8      0xa864e4d0
0x7c0c: 0xb0fa7502      0xe464e6d1      0x7502a864      0xe6dfb0fa
0x7c1c: 0x16010f60      0x200f7c78      0xc88366c0      0xc0220f01
0x7c2c: 0x087c31ea      0x10b86600      0x8ed88e00      0x66d08ec0
```

cs=8，在保护模式；ds=0x10`, `ss=0x10`, `es=0x10，在内核数据段

看 $esp，首先，栈的范围肯定是 esp=0x7bdc 到 ebp+4=0x7bfc，栈上的数据 0x7d8d 是 call bootmain 的返回地址，0x7c4d 是 bootmain 进行 call 时的返回地址，ebp=0x7bf8 其实是 bootmain 的栈底，后面这些数据都是机器码，可执行的指令。

```shell
(gdb) x/10i 0x7c00
   0x7c00:      cli
   0x7c01:      xor    %eax,%eax
   0x7c03:      mov    %eax,%ds
   0x7c05:      mov    %eax,%es
   0x7c07:      mov    %eax,%ss
   0x7c09:      in     $0x64,%al
   0x7c0b:      test   $0x2,%al
   0x7c0d:      jne    0x7c09
   0x7c0f:      mov    $0xd1,%al
   0x7c11:      out    %al,$0x64

```

顺便，这些指令是 bootloader，在 bootasm.S 开头就能看见

```asm
code16                       # Assemble for 16-bit mode
.globl start
start:
  cli                         # BIOS enabled interrupts; disable

  # Zero data segment registers DS, ES, and SS.
  xorw    %ax,%ax             # Set %ax to zero
  movw    %ax,%ds             # -> Data Segment
  movw    %ax,%es             # -> Extra Segment
  movw    %ax,%ss             # -> Stack Segment

```

回答一下下面的问题

> bootasm.S 中栈指针的初始化位置在哪里？

```shell
(gdb) 
The target architecture is assumed to be i386
=> 0x7c31:      mov    $0x10,%ax
0x00007c31 in ?? ()
(gdb) 
=> 0x7c35:      mov    %eax,%ds
0x00007c35 in ?? ()
(gdb) 
=> 0x7c37:      mov    %eax,%es
0x00007c37 in ?? ()
(gdb) 
=> 0x7c39:      mov    %eax,%ss
0x00007c39 in ?? ()
(gdb) 
=> 0x7c3b:      mov    $0x0,%ax
0x00007c3b in ?? ()
(gdb) 
=> 0x7c3f:      mov    %eax,%fs
0x00007c3f in ?? ()
(gdb) 
=> 0x7c41:      mov    %eax,%gs
0x00007c41 in ?? ()
(gdb) 
=> 0x7c43:      mov    $0x7c00,%esp
0x00007c43 in ?? ()
(gdb) 
=> 0x7c48:      call   0x7d3b
0x00007c48 in ?? ()
(gdb) reg
Undefined command: "reg".  Try "help".
(gdb) info reg
eax            0x0      0
ecx            0x0      0
edx            0x80     128
ebx            0x0      0
esp            0x7c00   0x7c00
ebp            0x0      0x0
esi            0x0      0
edi            0x0      0
eip            0x7c48   0x7c48
eflags         0x6      [ PF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x0      0
gs             0x0      0
(gdb) 
```

这里除了 call 0x7C48 都是在初始化。bootasm.S 相应的汇编语句为

```assembly
.code32  # Tell assembler to generate 32-bit code now.
start32:
  # Set up the protected-mode data segment registers
  movw    $(SEG_KDATA<<3), %ax    # Our data segment selector
  movw    %ax, %ds                # -> DS: Data Segment
  movw    %ax, %es                # -> ES: Extra Segment
  movw    %ax, %ss                # -> SS: Stack Segment
  movw    $0, %ax                 # Zero segments not ready for use
  movw    %ax, %fs                # -> FS
  movw    %ax, %gs                # -> GS

  # Set up the stack pointer and call into C.
  movl    $start, %esp
  call    bootmain
```

> 单步执行对 `bootmain` 的调用；现在堆栈上有什么？

```shell
(gdb) si
=> 0x7d3b:      push   %ebp
0x00007d3b in ?? ()
(gdb) info reg
eax            0x0      0
ecx            0x0      0
edx            0x80     128
ebx            0x0      0
esp            0x7bfc   0x7bfc
ebp            0x0      0x0
esi            0x0      0
edi            0x0      0
eip            0x7d3b   0x7d3b
eflags         0x6      [ PF ]
cs             0x8      8
ss             0x10     16
ds             0x10     16
es             0x10     16
fs             0x0      0
gs             0x0      0
(gdb) x/8x 0x7bfc
0x7bfc: 0x00007c4d      0x8ec031fa      0x8ec08ed8      0xa864e4d0
0x7c0c: 0xb0fa7502      0xe464e6d1      0x7502a864      0xe6dfb0fa
(gdb) x/8i $pc
=> 0x7d3b:      push   %ebp
   0x7d3c:      mov    %esp,%ebp
   0x7d3e:      push   %edi
   0x7d3f:      push   %esi
   0x7d40:      push   %ebx
   0x7d41:      sub    $0xc,%esp
   0x7d44:      push   $0x0
   0x7d46:      push   $0x1000
(gdb) 
```

现在 esp 变为了 0x7bfc，其中保存的值变成 0x7c4d，这个就是之前 7c48 之后一条语句的地址，就是返回地址。

可以看到现在就是在做一些压栈操作

> bootmain 的第一条汇编指令对堆栈做了什么？请在 bootblock.asm 文件中查找 bootmain。

同上，在压栈

bootblock.asm 中

```assembly
00007d3b <bootmain>:
{
    7d3b:	55                   	push   %ebp
    7d3c:	89 e5                	mov    %esp,%ebp
    7d3e:	57                   	push   %edi
    7d3f:	56                   	push   %esi
    7d40:	53                   	push   %ebx
    7d41:	83 ec 0c             	sub    $0xc,%esp
  readseg((uchar*)elf, 4096, 0);
    7d44:	6a 00                	push   $0x0
    7d46:	68 00 10 00 00       	push   $0x1000
    7d4b:	68 00 00 01 00       	push   $0x10000
    7d50:	e8 a3 ff ff ff       	call   7cf8 <readseg>
  if(elf->magic != ELF_MAGIC)
    7d55:	83 c4 0c             	add    $0xc,%esp
    7d58:	81 3d 00 00 01 00 7f 	cmpl   $0x464c457f,0x10000
    7d5f:	45 4c 46 
    7d62:	74 08                	je     7d6c <bootmain+0x31>
}
```

> 继续使用 gdb 进行跟踪（必要时使用断点——参见下方提示），查找将 `eip` 更改为 0x10000c 的调用。该调用对堆栈做了什么？

可以看到

![[MIT 6.828 HW 1] 1](img\[MIT 6.828 HW 1] 1.png)

这里马上要调用 0x10018

![[MIT 6.828 HW 1] 2](img\[MIT 6.828 HW 1] 2.png)

调用后如上，其实堆栈还是只有 esp eip 变了
