这个实验主要是实现 spawn 库调用，用来加载并运行磁盘上的可执行文件。然后会继续完善内核和库操作系统，最终达到写出一个 shell 的效果。当然，这也会涉及到文件系统的内容。

# Lab 5: File system, Spawn and Shell

## File system preliminaries

你将使用的文件系统比大多数“真实”文件系统（包括 xv6 UNIX 的文件系统）简单得多，但它足以提供基本功能：创建、读取、写入和删除以层次化目录结构组织的文件。

我们（至少目前）只是在开发一个单用户操作系统，它提供的保护足以捕获 bug，但并不用于保护多个互不信任的用户彼此隔离。因此，我们的文件系统不支持 UNIX 中的文件所有权或权限概念。我们的文件系统目前也不支持硬链接、符号链接、时间戳，或像大多数 UNIX 文件系统那样的特殊设备文件。

## On-Disk File System Structure

大多数 UNIX 文件系统把可用磁盘空间划分为两种主要区域：inode 区域和数据区域。UNIX 文件系统为文件系统中的每个文件分配一个 inode；文件的 inode 保存该文件的关键元数据，例如 stat 属性以及指向其数据块的指针。数据区域被划分成更大的（通常为 8KB 或更大）数据块，文件系统在其中存储文件数据和目录元数据。目录项包含文件名和指向 inode 的指针；如果文件系统中有多个目录项引用同一个文件的 inode，就称该文件是硬链接的。由于我们的文件系统不支持硬链接，因此不需要这一层间接性，于是可以做一个方便的简化：我们的文件系统完全不使用 inode，而是直接把一个文件（或子目录）的所有元数据存储在描述该文件的（唯一一个）目录项中。

文件和目录在逻辑上都由一系列数据块组成，这些数据块可能分散在整个磁盘上，就像一个环境的虚拟地址空间中的页面可能分散在物理内存中一样。文件系统环境隐藏块布局的细节，提供接口，用于在文件中的任意偏移处读取和写入字节序列。文件系统环境会在执行文件创建和删除等操作时，在内部处理对目录的所有修改。我们的文件系统确实允许用户环境直接读取目录元数据（例如通过 `read`），这意味着用户环境可以自行执行目录扫描操作（例如实现 `ls` 程序），而不必依赖文件系统提供额外的特殊调用。采用这种目录扫描方式的缺点，也是大多数现代 UNIX 变体不鼓励这样做的原因，是它会使应用程序依赖目录元数据的格式，从而使得在不修改或至少不重新编译应用程序的情况下，很难改变文件系统的内部布局。

## Sectors and Blocks

大多数磁盘不能以字节粒度进行读写，而是以扇区为单位进行读写。在 JOS 中，每个扇区为 512 字节。文件系统实际以块为单位分配和使用磁盘存储。请注意区分这两个术语：扇区大小是磁盘硬件的属性，而块大小是使用该磁盘的操作系统的一个方面。文件系统的块大小必须是底层磁盘扇区大小的倍数。

UNIX xv6 文件系统使用 512 字节的块大小，与底层磁盘的扇区大小相同。然而，大多数现代文件系统使用更大的块大小，因为存储空间已经变得便宜得多，并且以更大的粒度管理存储更加高效。我们的文件系统将使用 4096 字节的块大小，这正好与处理器的页大小一致。

## Superblocks

### Disk layout

文件系统通常会在磁盘上某些“容易找到”的位置（例如磁盘开头或结尾）保留特定磁盘块，用来保存描述整个文件系统属性的元数据，例如块大小、磁盘大小、寻找根目录所需的任何元数据、文件系统上次挂载的时间、文件系统上次检查错误的时间，等等。这些特殊块称为超级块。

我们的文件系统将只有一个超级块，并且它总是位于磁盘的第 1 块。它的布局由 `inc/fs.h` 中的 `struct Super` 定义。第 0 块通常保留用于保存引导加载器和分区表，因此文件系统通常不使用磁盘上的第一个磁盘块。许多“真实”的文件系统维护多个超级块，它们复制分布在磁盘上多个相隔较远的区域中，这样如果其中一个超级块损坏，或者磁盘在该区域出现介质错误，仍然可以找到并使用其他超级块来访问文件系统。

## File Meta-data

### File structure

在我们的文件系统中，描述文件的元数据布局由 `inc/fs.h` 中的 `struct File` 描述。这些元数据包括文件名、大小、类型（普通文件或目录），以及指向组成该文件的块的指针。如前所述，我们没有 inode，因此这些元数据存储在磁盘上的目录项中。与大多数“真实”文件系统不同，为了简单起见，我们将使用同一个 `File` 结构来表示磁盘上和内存中的文件元数据。

`struct File` 中的 `f_direct` 数组为文件的前 10 个（`NDIRECT`）块的块号提供存储空间，我们称这些块为文件的直接块。对于大小不超过 `10*4096 = 40KB` 的小文件，这意味着该文件所有块的块号都可以直接存放在 `File` 结构本身中。然而，对于更大的文件，我们需要一个地方来保存其余文件块的块号。因此，对于任何大小超过 40KB 的文件，我们会额外分配一个磁盘块，称为该文件的间接块，用于保存最多 `4096/4 = 1024` 个额外块号。因此，我们的文件系统允许文件最大为 1034 个块，即略大于 4MB。为了支持更大的文件，“真实”的文件系统通常还会支持二级和三级间接块。

## Directories versus Regular Files

我们文件系统中的 `File` 结构既可以表示普通文件，也可以表示目录；这两种“文件”通过 `File` 结构中的 `type` 字段区分。文件系统以完全相同的方式管理普通文件和目录文件，区别在于它完全不解释普通文件关联的数据块内容，而会把目录文件的内容解释为一系列 `File` 结构，这些结构描述该目录中的文件和子目录。

我们的文件系统中的超级块包含一个 `File` 结构（`struct Super` 中的 `root` 字段），它保存文件系统根目录的元数据。这个目录文件的内容是一系列 `File` 结构，描述位于文件系统根目录中的文件和目录。根目录中的任何子目录又可以包含更多 `File` 结构，用来表示子子目录，依此类推。

![[MIT 6.828 Lab 5] 1](img\[MIT 6.828 Lab 5] 1.png)

## The File System

本实验的目标不是让你实现整个文件系统，而是只实现其中某些关键组件。具体来说，你将负责把块读入块缓存并将其刷新回磁盘；分配磁盘块；把文件偏移映射到磁盘块；以及在 IPC 接口中实现 `read`、`write` 和 `open`。由于你并不会自己实现整个文件系统，因此非常重要的一点是，你要熟悉所提供的代码以及各种文件系统接口。

## Disk Access

我们操作系统中的文件系统环境需要能够访问磁盘，但我们还没有在内核中实现任何磁盘访问功能。我们没有采用传统“单体”操作系统的策略，即在内核中添加 IDE 磁盘驱动以及必要的系统调用以允许文件系统访问磁盘，而是把 IDE 磁盘驱动作为用户级文件系统环境的一部分来实现。不过，我们仍然需要稍微修改内核，以便进行设置，让文件系统环境拥有自己实现磁盘访问所需的权限。

只要我们依赖轮询式的、基于“程序化 I/O”（PIO）的磁盘访问，并且不使用磁盘中断，就很容易以这种方式在用户空间实现磁盘访问。也可以在用户模式下实现中断驱动的设备驱动（例如 L3 和 L4 内核就是这样做的），但这更困难，因为内核必须接收设备中断并将其分派给正确的用户模式环境。

x86 处理器使用 EFLAGS 寄存器中的 IOPL 位来决定受保护模式代码是否允许执行特殊的设备 I/O 指令，例如 `IN` 和 `OUT` 指令。由于我们需要访问的所有 IDE 磁盘寄存器都位于 x86 的 I/O 空间中，而不是内存映射空间中，因此给文件系统环境授予“I/O 权限”就是允许文件系统访问这些寄存器所需做的唯一事情。实际上，EFLAGS 寄存器中的 IOPL 位为内核提供了一种简单的“全有或全无”方式，用于控制用户模式代码是否可以访问 I/O 空间。在我们的例子中，我们希望文件系统环境能够访问 I/O 空间，但不希望任何其他环境能够访问 I/O 空间。

### Exercise 1

> `i386_init` 通过向你的环境创建函数 `env_create` 传递类型 `ENV_TYPE_FS` 来识别文件系统环境。修改 `env.c` 中的 `env_create`，使其给文件系统环境授予 I/O 权限，但绝不把该权限授予任何其他环境。
>
> 确保你可以启动文件系统环境而不会导致 General Protection fault。你应该通过 `make grade` 中的 `fs i/o` 测试。

根据讲义，与传统的内核把磁盘驱动塞在内核里不同，JOS 把 IDE 磁盘驱动做成了一个运行在用户态的特殊进程，即文件系统环境 FS env。

既然是在用户态，常规进程是没有权限去用 inb、outb 这些 IO 指令读写硬件端口的，所以得我们手动去授权。在 x86 中，控制 I/O 权限的是 EFLAGS 寄存器里的 IOPL（I/O Privilege Level）位。

首先用 env_create 函数判断一下传进来的 type 是不是 ENV_TYPE_FS，如果是，就把 Trapframe 里 EFLAGS 寄存器的 IOPL 位置为 3，表示允许 R3 级别的用户态代码执行 IO 操作。

```c
if (type == ENV_TYPE_FS) {
    e->env_tf.tf_eflags |= FL_IOPL_3;
}
```

### Question 1

> 1. 当你随后从一个环境切换到另一个环境时，是否还需要做其他事情来确保这个 I/O 权限设置被正确保存和恢复？为什么？

不需要做其他事情了。进程的状态是保存在每个环境的 `struct Trapframe` 里面的，EFLAGS 寄存器也是 Trapframe 的一部分：`env_tf.tf_eflags`。

当我们发生中断或进行系统调用而陷入内核时，硬件会自动把当前的 EFLAGS 压入内核栈中保存起来。当我们调用 env_run 切换回任意进程时，会调用 env_pop_tf，其中的 iret 指令会自动把栈里保存的 EFLAGS 弹出并恢复到 CPU 寄存器中。 所以，IOPL 的状态完全跟着 Trapframe 的状态进行保存与恢复，不需要再去写额外的代码来单独维护它。

---

请注意，本实验中的 `GNUmakefile` 文件会设置 QEMU 像以前一样使用文件 `obj/kern/kernel.img` 作为磁盘 0（在 DOS/Windows 下通常是“Drive C”）的镜像，并使用（新的）文件 `obj/fs/fs.img` 作为磁盘 1（“Drive D”）的镜像。在本实验中，我们的文件系统应该只接触磁盘 1；磁盘 0 仅用于启动内核。如果你以某种方式破坏了任一磁盘镜像，可以通过输入以下命令将它们重置为原始的“干净”版本：

```text
$ rm obj/kern/kernel.img obj/fs/fs.img
$ make
```

或者执行：

```text
$ make clean
$ make
```

## The Block Cache

在我们的文件系统中，我们将借助处理器的虚拟内存系统来实现一个简单的“缓冲区缓存”（实际上只是块缓存）。块缓存的代码位于 `fs/bc.c`。

我们的文件系统将限制为只处理大小不超过 3GB 的磁盘。我们在文件系统环境的地址空间中保留一个较大的固定 3GB 区域，从 `0x10000000`（`DISKMAP`）到 `0xD0000000`（`DISKMAP+DISKMAX`），作为磁盘的一个“内存映射”版本。例如，磁盘块 0 映射到虚拟地址 `0x10000000`，磁盘块 1 映射到虚拟地址 `0x10001000`，依此类推。`fs/bc.c` 中的 `diskaddr` 函数实现了从磁盘块号到虚拟地址的转换（并进行一些合理性检查）。

由于文件系统环境拥有自己的虚拟地址空间，独立于系统中所有其他环境的虚拟地址空间，并且文件系统环境唯一需要做的事就是实现文件访问，因此以这种方式保留文件系统环境地址空间的大部分是合理的。在 32 位机器上的真实文件系统实现中这样做会比较笨拙，因为现代磁盘都大于 3GB。不过，在拥有 64 位地址空间的机器上，这样的缓冲区缓存管理方法仍然可能是合理的。

当然，把整个磁盘读入内存会花很长时间，因此我们将实现一种按需分页形式：只有当磁盘映射区域发生页错误时，才在该区域分配页面，并从磁盘读取相应块。这样，我们就可以假装整个磁盘都在内存中。

### Exercise 2

> 实现 `fs/bc.c` 中的 `bc_pgfault` 和 `flush_block` 函数。`bc_pgfault` 是一个页错误处理程序，就像你在上一个实验中为写时复制 fork 编写的页错误处理程序一样，只不过它的任务是在页错误发生时从磁盘加载页面。编写时请记住：（1）`addr` 可能没有按块边界对齐；（2）`ide_read` 以扇区为单位操作，而不是以块为单位。
>
> `flush_block` 函数应该在必要时把一个块写回磁盘。如果该块甚至不在块缓存中（也就是说，该页没有映射），或者它不是脏的，`flush_block` 不应该做任何事情。我们将使用 VM 硬件来跟踪一个磁盘块自上次从磁盘读取或写入磁盘以来是否被修改过。要判断某个块是否需要写入，只需查看 `uvpt` 项中是否设置了 `PTE_D` “dirty” 位即可。（处理器会在写入该页时设置 `PTE_D` 位；参见 386 参考手册第 5 章中的 5.2.4.3。）在把块写入磁盘之后，`flush_block` 应该使用 `sys_page_map` 清除 `PTE_D` 位。
>
> 使用 `make grade` 测试你的代码。你的代码应该通过 `check_bc`、`check_super` 和 `check_bitmap`。

可以发现，JOS 在文件系统的设计上没有去维护一个复杂的缓冲区队列，而是类似于虚拟内存的按需分页。

根据讲义，JOS 允许磁盘最大为 3GB。文件系统进程在自己的虚拟地址空间里预留了一块 3GB 的连续区域，起始地址 DISKMAP 0x10000000。它假装整个磁盘的内容都已经在这个内存里了：磁盘块 0 对应 0x10000000，磁盘块 1 对应 0x10001000，以此类推。

但实际上，初始化的时候这里根本没分配物理内存。当文件系统的代码去读写这块区域时，肯定会触发缺页异常。这个时候就该触发 page fault 了，也就是这里要写的 bc_pgfault，它负责临时去物理内存里找一页，然后把磁盘上对应的数据读进来。

首先写 bc_pgfault。这里注意，缺页的地址 addr 可能是页内的一个任意偏移地址，所以得先把它按页大小向下对齐。磁盘 IO 驱动 ide_read 认的是扇区（Sector，512 字节），不是块（Block，4096 字节）。一个 Block 等于 8 个 Sector，读磁盘的时候记得把块号换算成扇区号。

```c
// LAB 5: you code here:
// 将 addr 向下对齐到页边界
addr = ROUNDDOWN(addr, PGSIZE);

// 为当前环境分配一个物理页，映射到 addr 处
// 权限 PTE_W | PTE_U | PTE_P，因为一会儿 ide_read 要往里面写，后续文件系统代码也要读写
r = sys_page_alloc(0, addr, PTE_W | PTE_U | PTE_P);
if (r < 0) {
    panic("in bc_pgfault, sys_page_alloc failed: %e", r);
}

// 从磁盘读取数据填满这个页
// block = BLKSIZE / SECTSIZE，即 4096/512 = 8 个扇区。起始扇区号就是 blockno * 8
r = ide_read(blockno * (BLKSIZE / SECTSIZE), addr, BLKSIZE / SECTSIZE);
if (r < 0) {
    panic("in bc_pgfault, ide_read failed: %e", r);
}
```

然后 flush_block，这个就是把内存里修改过的数据写回磁盘。

我们需要检查这个块到底有没有被调入内存，如果它不在页表中，就直接返回。如果在内存里，还要检查它有没有被修改过。这里我们直接读取用户侧的 uvpt，看看对应的页表项里面 PTE_D 是否被置 1 了，x86 硬件会在我们向页面写入数据时自动把这个位置 1。

确认要写回后，调用 ide_write 写磁盘，最后重新 map 一次页面，带上除 PTE_D 外原有的读写权限。

```c
// LAB 5: Your code here.
int r;
// 将 addr 向下对齐到页边界
addr = ROUNDDOWN(addr, PGSIZE);

// 检查该地址是否映射，以及是否被修改过
if (!va_is_mapped(addr) || !(uvpt[PGNUM(addr)] & PTE_D)) {
    return;
}

// 写回磁盘
r = ide_write(blockno * (BLKSIZE / SECTSIZE), addr, BLKSIZE / SECTSIZE);
if (r < 0) {
    panic("in flush_block, ide_write failed: %e", r);
}

// 用 sys_page_map 重新映射，把 PTE_D 清零
// PTE_SYSCALL 掩码包含了所有的用户权限位（PTE_U, PTE_W, PTE_P 等），但排除了硬件维护的 PTE_D 和 PTE_A
r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL);
if (r < 0) {
    panic("in flush_block, sys_page_map failed: %e", r);
}
```
跑 make grade 测试前记得检查 kern/init.c，创建 `ENV_CREATE(fs_fs, ENV_TYPE_FS)`。我这里可能是 merge 的时候 conflict 没处理好？

这个最好直接放宏定义外面，make grade 会在编译参数里面加 -DTEST=xxx，TEST 这个宏在 make grade 时是 defined 的。

```c
	ENV_CREATE(fs_fs, ENV_TYPE_FS);
#if defined(TEST)
	// Don't touch -- used by grading script!
	ENV_CREATE(TEST, ENV_TYPE_USER);
#else
	// Touch all you want.
	ENV_CREATE(user_dumbfork, ENV_TYPE_USER);
```

然后 make grade 就能看见

![[MIT 6.828 Lab 5] 2](img\[MIT 6.828 Lab 5] 2.png)

---

`fs/fs.c` 中的 `fs_init` 函数是如何使用块缓存的一个典型例子。初始化块缓存之后，它只是把指向磁盘映射区域的指针存储到全局变量 `super` 中。从这一点开始，我们就可以像读取内存中的结构一样直接读取 `super` 结构，页错误处理程序会在必要时从磁盘读取相应内容。

## The Block Bitmap

在 `fs_init` 设置 `bitmap` 指针之后，我们可以把 `bitmap` 当作一个紧凑的位数组来处理，其中每一位对应磁盘上的一个块。参见例如 `block_is_free`，它只是检查给定块是否在位图中标记为空闲。

### Exercise 3

> 以 `free_block` 为模型，实现 `fs/fs.c` 中的 `alloc_block`，它应该在位图中找到一个空闲磁盘块，将其标记为已使用，并返回该块的编号。分配块时，你应该立即使用 `flush_block` 将发生变化的位图块刷新到磁盘，以帮助维护文件系统一致性。
>
> 使用 `make grade` 测试你的代码。你的代码现在应该通过 `alloc_block`。

操作系统管理空闲磁盘块最常用的数据结构就是位图 Bitmap。

JOS 的文件系统位图规则是：位值为 1 表示空闲，位值为 0 表示已使用，参考 free_block，它用 `bitmap[blockno / 32] |= 1 << (blockno % 32)` 把对应的位置 1，然后 `flush_block(&bitmap[blockno / 32])` 把这块内存写回磁盘。那么 alloc_block 就是反过来，遍历所有可能的数据块，用自带的 block_is_free 检查它是不是空闲的。如果是，就用按位与非把这一的位置 0。

注意，我们需要写回的是被修改的位图所在的那个内存页，而不是刚分配的那个数据块。

```c
// LAB 5: Your code here.
// panic("alloc_block not implemented");
uint32_t blockno;

// 遍历整个磁盘的块，查找空闲块
for (blockno = 0; blockno < super->s_nblocks; blockno++) {
    // 判断这一位是不是 1 
    if (block_is_free(blockno)) {
        // 清 0，标记为已使用
        bitmap[blockno / 32] &= ~(1 << (blockno % 32));

        // 把被修改的位图数据写回磁盘
        flush_block(&bitmap[blockno / 32]);

        return blockno;
    }
}

// 磁盘空间已满
return -E_NO_DISK;
```

现在 `alloc_block: OK`

## File Operations

我们在 `fs/fs.c` 中提供了多种函数，用于实现你解释和管理 `File` 结构、扫描和管理目录文件条目，以及从根目录开始遍历文件系统以解析绝对路径名所需的基本功能。在继续之前，请通读 `fs/fs.c` 中的所有代码，并确保你理解每个函数的作用。

### Exercise 4

> 实现 `file_block_walk` 和 `file_get_block`。`file_block_walk` 将文件内的块偏移映射到 `struct File` 中或间接块中的相应块指针，这非常类似于 `pgdir_walk` 对页表所做的事情。`file_get_block` 更进一步，映射到实际磁盘块，并在必要时分配一个新块。
>
> 使用 `make grade` 测试你的代码。你的代码应该通过 `file_open`、`file_get_block`、`file_flush/file_truncated/file rewrite` 和 `testfile`。

这里确实跟 pgdir_walk 很像。内存管理 pgdir_walk 的时候是页目录 -> 页表 -> 物理页，这里文件系统 file_block_walk 则是文件数据结构 struct File -> 间接块 Indirect Block -> 磁盘数据块 Block。

JOS 文件系统的 struct File 结构里存了文件的元数据。为了支持大文件，JOS 采用了直接块 + 间接块混合的结构。

f_direct[NDIRECT]的前 10 个是直接块指针，里面存的直接就是磁盘块号。文件小于 40KB 的话，直接在这里找就完事了。f_indirect 第 11 个指针是一个一级间接块指针。它指向一个单独的磁盘块，这个磁盘块里存了 1024 个块号（4096 / 4 字节），所以文件的最大体积可以扩展到 1034 个块。

现在先实现 file_block_walk，这个函数的核心目的是根据文件内偏移 filebno，找出一个指针。这个指针指向你要找的那个磁盘块的块号存放位置。注意，我们要返回的是“存块号的那个地址”，即指针的指针 ppdiskbno。如果对应的间接块没分配且 alloc 为真，那么我们要给间接块分配空间。

```c
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
    // LAB 5: Your code here.
    // panic("file_block_walk not implemented");
	int r;
    uint32_t *indirects;

    // 如果块号超出了支持的最大范围 10 + 1024 = 1034，报错
    if (filebno >= NDIRECT + NINDIRECT) {
        return -E_INVAL;
    }

    // 如果是直接块，在 f_direct 里找
    if (filebno < NDIRECT) {
        if (ppdiskbno) {
            *ppdiskbno = &(f->f_direct[filebno]);
        }
        return 0;
    }

    // 要找的是间接块里面的

    // 检查一级间接块有没有分配
    if (f->f_indirect == 0) {

        if (alloc == 0) {
            return -E_NOT_FOUND;
        }
        
        // 分配一个物理磁盘块来当间接块
        r = alloc_block();
        if (r < 0) {
            return r; // 没磁盘空间了
        }
        
        // 记录块号，并将这个新的间接块清零，然后写回磁盘
        f->f_indirect = r;
        memset(diskaddr(r), 0, BLKSIZE);
        flush_block(diskaddr(r));
    }

    // 找到间接块映射在内存中的起始虚拟地址
    indirects = (uint32_t *) diskaddr(f->f_indirect);
    
    // 注意索引要减去前 10 个直接块的偏移
    if (ppdiskbno) {
        *ppdiskbno = &(indirects[filebno - NDIRECT]);
    }

    return 0;
	
}
```

然后是 file_get_block。file_block_walk 是为了找指针存放的位置，file_get_block 则是把块里的数据加载出来。如果那个磁盘块号是 0，说明文件在这个偏移还没写入过数据，就分配一个新块。

```c
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
    // LAB 5: Your code here.
    // panic("file_get_block not implemented");
	int r;
    uint32_t *pdiskbno;

    // 找到对应的块号指针
    r = file_block_walk(f, filebno, &pdiskbno, 1);
    if (r < 0) {
        return r;
    }

    // 如果指针里的值是 0，说明具体的数据块还没分配
    if (*pdiskbno == 0) {
        r = alloc_block();
        if (r < 0) {
            return r;
        }
        *pdiskbno = r;
        
        // 新分配的数据块清零，防止读到旧数据
        memset(diskaddr(r), 0, BLKSIZE);
        flush_block(diskaddr(r));
    }

    // 将对应磁盘块映射在内存里的虚拟地址返回给 blk
    if (blk) {
        *blk = diskaddr(*pdiskbno);
    }

    return 0;
}
```

![[MIT 6.828 Lab 5] 3](img\[MIT 6.828 Lab 5] 3.png)

通过了，剩下的那些 FAIL 和 MISSING 是下个 Exercise 的

---

`file_block_walk` 和 `file_get_block` 是文件系统的核心工作函数。例如，`file_read` 和 `file_write` 基本上只是建立在 `file_get_block` 之上的一些簿记逻辑，用于在分散的块和连续缓冲区之间复制字节。

## The file system interface

现在我们已经在文件系统环境本身内部拥有了必要功能，必须让希望使用文件系统的其他环境能够访问它。由于其他环境不能直接调用文件系统环境中的函数，我们将通过一个远程过程调用（RPC）抽象来暴露文件系统环境的访问能力；该抽象建立在 JOS 的 IPC 机制之上。下图展示了对文件系统服务器的一次调用（例如 `read`）的大致过程：

```text
      Regular env           FS env
   +---------------+   +---------------+
   |      read     |   |   file_read   |
   |   (lib/fd.c)  |   |   (fs/fs.c)   |
...|.......|.......|...|.......^.......|...............
   |       v       |   |       |       | RPC mechanism
   |  devfile_read |   |  serve_read   |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |     fsipc     |   |     serve     |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |   ipc_send    |   |   ipc_recv    |
   |       |       |   |       ^       |
   +-------|-------+   +-------|-------+
           |                   |
           +-------------------+
```

虚线以下的一切都只是把一个读请求从普通环境传递到文件系统环境的机制。从开头看起，`read`（我们已提供）可以作用于任何文件描述符，并简单地分派到合适的设备读取函数，在这个例子中就是 `devfile_read`（我们还可以拥有更多设备类型，例如管道）。`devfile_read` 专门实现磁盘文件的读取。这个函数以及 `lib/file.c` 中其他 `devfile_*` 函数实现了 FS 操作的客户端部分，并且工作方式大致相同：把参数打包到一个请求结构中，调用 `fsipc` 发送 IPC 请求，然后解包并返回结果。`fsipc` 函数只是处理向服务器发送请求和接收响应的公共细节。

文件系统服务器代码位于 `fs/serv.c`。它在 `serve` 函数中循环，不断通过 IPC 接收请求，将该请求分派到相应的处理函数，然后通过 IPC 发回结果。在 `read` 的例子中，`serve` 会分派到 `serve_read`，后者负责处理 read 请求特有的 IPC 细节，例如解包请求结构，最终调用 `file_read` 来实际执行文件读取。

回忆一下，JOS 的 IPC 机制允许一个环境发送一个 32 位数，并可选地共享一个页面。为了从客户端向服务器发送请求，我们使用这个 32 位数表示请求类型（文件系统服务器 RPC 的编号方式就像系统调用编号一样），并把请求参数存储在通过 IPC 共享的页面中的一个 `union Fsipc` 中。在客户端，我们总是在 `fsipcbuf` 处共享该页；在服务器端，我们把传入的请求页映射到 `fsreq`（`0x0ffff000`）。

服务器也通过 IPC 发回响应。我们使用 32 位数表示函数的返回码。对于大多数 RPC，这就是它们返回的全部内容。`FSREQ_READ` 和 `FSREQ_STAT` 也会返回数据，它们只是把数据写入客户端发送请求时共享的那个页面。响应 IPC 中无需再次发送该页，因为客户端一开始就已经把它与文件系统服务器共享了。此外，在 `FSREQ_OPEN` 的响应中，服务器会与客户端共享一个新的“Fd page”。我们很快会回到文件描述符页面。

### Exercise 5

> 实现 `fs/serv.c` 中的 `serve_read`。
>
> `serve_read` 的主要工作将由已经实现的 `fs/fs.c` 中的 `file_read` 完成（而 `file_read` 本身又只是一堆对 `file_get_block` 的调用）。`serve_read` 只需要为文件读取提供 RPC 接口。查看 `serve_set_size` 中的注释和代码，以大致了解服务器函数应该如何组织。
>
> 使用 `make grade` 测试你的代码。你的代码应该通过 `serve_open/file_stat/file_close` 和 `file_read`，分数达到 70/150。

这里就是一个 C/S 架构，从讲义可以看出，JOS 把文件系统做成了一个独立的服务端进程 FS env。其他普通的用户进程（client）如果想读写文件，不能直接去调底层的函数，必须得通过 IPC 来发送请求（RPC）。

那么这里的流程就是：客户端发起请求、服务端接收、查表并定位文件、读数据、更新偏移、返回给客户端。

```c
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;

	// if (debug)
	// 	cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Lab 5: Your code here:
	struct OpenFile *o;
	int r;

	// 查找文件 ID 是否合法，获取对应的 OpenFile 结构体
	r = openfile_lookup(envid, req->req_fileid, &o);
	if (r < 0) {
		return r;
	}

	// 将数据读取到返回用的共享内存缓冲区中
	r = file_read(o->o_file, ret->ret_buf, req->req_n, o->o_fd->fd_offset); // file_read(文件结构体, 读到哪, 读多少, 从哪个偏移量开始读)
	if (r < 0) {
		return r;
	}

	// 更新当前文件的读取偏移量
	o->o_fd->fd_offset += r;

	// 返回实际读取的字节数
	return r;
}
```

### Exercise 6

> 实现 `fs/serv.c` 中的 `serve_write` 和 `lib/file.c` 中的 `devfile_write`。
>
> 使用 `make grade` 测试你的代码。你的代码应该通过 `file_write`、`file_read after file_write`、`open` 和 `large file`，分数达到 90/150。

跟 Exercise 5 配套的。

读文件是服务端把数据放进共享物理页传回来，写文件就是客户端把要写的数据先放进共享物理页里，然后发个 IPC 告诉服务端去写磁盘。

这里需要注意，IPC 通信双方共享的数据区`fsipcbuf.write.req_buf`的大小是有限制的。如果用户程序传进来的写入字节数 n 超过了这个最大值，我们就只截取前面的部分发给服务端。外层的 write 系统库函数如果发现没写够 n 个字节，它会自动在一个循环里不断调 devfile_write 继续写，这是流式写入的标准逻辑，所以不用担心会把剩下的数据截断掉导致没写入完整。

那么首先实现客户端 devfile_write，我们要做的就是把请求打包，限制数据大小，然后通过 fsipc 发送 FSREQ_WRITE 类型的 RPC 给服务端。

```c
static ssize_t
devfile_write(struct Fd *fd, const void *buf, size_t n)
{
	// Make an FSREQ_WRITE request to the file system server.  Be
	// careful: fsipcbuf.write.req_buf is only so large, but
	// remember that write is always allowed to write *fewer*
	// bytes than requested.
	// LAB 5: Your code here
	// panic("devfile_write not implemented");

	int r;

	// 设置文件 ID
	fsipcbuf.write.req_fileid = fd->fd_file.id;

	// 限制单次写入的最大字节数，防止溢出
	fsipcbuf.write.req_n = MIN(n, sizeof(fsipcbuf.write.req_buf));

	// 将用户提供的数据复制进 IPC 共享缓冲中
	memmove(fsipcbuf.write.req_buf, buf, fsipcbuf.write.req_n);

	// 发起 IPC 调用，请求服务端写入磁盘
	r = fsipc(FSREQ_WRITE, NULL);
	
	// 实际写入的字节数或者错误码
	return r;
}
```

然后是服务端 serve_write，这就完全是 serve_read 的翻版了，通过文件 ID 获取文件控制块，调用 file_write，最后更新偏移量。

```c
int
serve_write(envid_t envid, struct Fsreq_write *req)
{
	// if (debug)
	// 	cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// LAB 5: Your code here.
	// panic("serve_write not implemented");
	struct OpenFile *o;
	int r;
	// 查找对应的打开文件结构
	r = openfile_lookup(envid, req->req_fileid, &o);
	if (r < 0) {
		return r;
	}

	// 调用 file_write 往块缓存里写数据
	r = file_write(o->o_file, req->req_buf, req->req_n, o->o_fd->fd_offset);
	if (r < 0) {
		return r;
	}

	// 成功写入后更新文件偏移量
	o->o_fd->fd_offset += r;

	// 成功写入的字节数
	return r;
}
```

两个 Exercise 做完后 make grade

![[MIT 6.828 Lab 5] 4](img\[MIT 6.828 Lab 5] 4.png)

## Spawning Processes

我们已经为你提供了 `spawn` 的代码（见 `lib/spawn.c`），它会创建一个新环境，从文件系统中加载一个程序镜像到该环境中，然后启动子环境运行这个程序。父进程随后继续独立于子进程运行。`spawn` 函数实际上相当于 UNIX 中的 `fork` 后紧接着在子进程中执行 `exec`。

我们实现 `spawn` 而不是 UNIX 风格的 `exec`，是因为在“外核风格”（exokernel fashion）下，`spawn` 更容易从用户空间实现，不需要内核提供特殊帮助。思考一下，如果要在用户空间实现 `exec`，你需要做什么，并确保你理解为什么它更困难。

### Exercise 7

> `spawn` 依赖新的系统调用 `sys_env_set_trapframe` 来初始化新创建环境的状态。实现 `kern/syscall.c` 中的 `sys_env_set_trapframe`（不要忘记在 `syscall()` 中分派这个新的系统调用）。
>
> 通过从 `kern/init.c` 运行 `user/spawnhello` 程序来测试你的代码，该程序会尝试从文件系统中 spawn `/hello`。
>
> 使用 `make grade` 测试你的代码。

在 UNIX 里，跑一个新程序一般是 fork() 一个子进程，然后在子进程里调 exec() 让内核去解析可执行文件并覆盖内存。 JOS 的 spawn 则是在用户态就把活干完了。父进程（这里就是 spawn 库）自己去读取文件系统里的 ELF 文件，自己申请内存页，自己把程序的各个段装载到子进程的内存里。

但是在这个过程中，父进程面临一个问题：它需要设置子进程的初始运行状态，但用户程序是不能直接修改另一个进程的内部状态的。所以，我们需要写一个系统调用 sys_env_set_trapframe，让父进程把准备好的 Trapframe 打包发给内核，内核验证没问题后，直接放进子进程的 env_tf 里。

首先实现系统调用，根据注释调整 env_tf

```c
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
```

然后注册系统调用

```c
case SYS_env_set_trapframe:
	return sys_env_set_trapframe(a1, (struct Trapframe *)a2);
```

在 init.c 中把测试改成 `ENV_CREATE(user_spawnhello, ENV_TYPE_USER);`，随后

![[MIT 6.828 Lab 5] 5](img\[MIT 6.828 Lab 5] 5.png)

可以看见 `i am parent environment 00001001` 是 user/spawnhello.c 的输出，`hello, world` 和 `i am environment 00001002` 则是读取了 /hello 并作为子进程运行。最后`No runnable environments in the system!`退出。

## Sharing library state across fork and spawn

UNIX 文件描述符是一个通用概念，也包括管道、控制台 I/O 等。在 JOS 中，每种设备类型都有一个对应的 `struct Dev`，其中包含指向实现该设备类型的 `read/write/etc.` 函数的指针。`lib/fd.c` 在此基础上实现了通用的类 UNIX 文件描述符接口。每个 `struct Fd` 指明其设备类型，而 `lib/fd.c` 中的大多数函数只是把操作分派到相应 `struct Dev` 中的函数。

`lib/fd.c` 还在每个应用程序环境的地址空间中维护文件描述符表区域，该区域从 `FDTABLE` 开始。这个区域为应用程序最多可以同时打开的 `MAXFD`（当前为 32）个文件描述符中的每一个保留一页（4KB）地址空间。在任意时刻，某个特定的文件描述符表页面被映射，当且仅当相应的文件描述符正在使用。每个文件描述符在从 `FILEDATA` 开始的区域中还可以拥有一个可选的“数据页”，设备可以按需使用。

我们希望在 `fork` 和 `spawn` 之间共享文件描述符状态，但文件描述符状态保存在用户空间内存中。目前，在 `fork` 时，这些内存会被标记为写时复制，因此状态会被复制，而不是共享。（这意味着环境无法在不是自己打开的文件中执行 seek，并且管道无法跨 fork 工作。）在 `spawn` 时，这些内存会被留下，完全不会复制。（实际上，被 spawn 的环境开始时没有任何打开的文件描述符。）

我们将修改 `fork`，让它知道某些内存区域由“库操作系统”使用，并且应该始终被共享。我们不会在某个地方硬编码区域列表，而是会在页表项中设置一个原本未使用的位（就像我们在 fork 中使用 `PTE_COW` 位一样）。

我们在 `inc/lib.h` 中定义了一个新的 `PTE_SHARE` 位。这个位是 Intel 和 AMD 手册中标记为“可供软件使用”的三个 PTE 位之一。我们将建立这样的约定：如果一个页表项设置了该位，那么在 `fork` 和 `spawn` 中都应该直接把该 PTE 从父环境复制到子环境。注意，这不同于把它标记为写时复制：如第一段所述，我们希望确保对页面的更新被共享。

### Exercise 8

> 修改 `lib/fork.c` 中的 `duppage` 以遵循新的约定。如果页表项设置了 `PTE_SHARE` 位，就直接复制该映射。（你应该使用 `PTE_SYSCALL`，而不是 `0xfff`，来屏蔽页表项中的相关位。`0xfff` 也会包含 accessed 和 dirty 位。）
>
> 同样地，实现 `lib/spawn.c` 中的 `copy_shared_pages`。它应该遍历当前进程中的所有页表项（就像 `fork` 做的那样），把任何设置了 `PTE_SHARE` 位的页映射复制到子进程中。
>
> 使用 `make run-testpteshare` 检查你的代码行为是否正确。你应该看到类似 `fork handles PTE_SHARE right` 和 `spawn handles PTE_SHARE right` 的输出行。
>
> 使用 `make run-testfdsharing` 检查文件描述符是否被正确共享。你应该看到类似 `read in child succeeded` 和 `read in parent succeeded` 的输出行。

这里是在解决文件描述符的继承与共享的问题。

在 UNIX 里，如果你 open 了一个文件，然后 fork 出一个子进程，父子进程是共享这个文件的读写偏移量的。父进程读了 10 个字节，偏移量后移，子进程接着读的话，会从第 11 个字节开始读。管道能通信也是依赖这种共享。

但是在 Lab 4 写的 fork 里，内存全是写时复制的，这意味着如果父进程更新了文件偏移量，触发了 Page Fault，系统会给父进程复制一份新的内存页，子进程压根看不见这个更新。spawn 现在甚至啥都不给子进程复制，子进程一开始只是个空壳。

为了解决这个问题，JOS 利用页表项里空闲的位（Intel 留了 3 个 bit 给软件随便用），定义了一个叫 PTE_SHARE 的标志位。 只要某块内存的页表项被打上了 PTE_SHARE 标签，我们在 fork 和 spawn 的时候就不写时复制，而是直接把父进程和子进程的虚拟地址映射到同一块物理内存上。

首先修改 duppage，加上对 PTE_SHARE 的判断。如果遇到了这个标志位，直接调用 sys_page_map 把当前物理页映射给子进程，并且权限完全照搬

```c
pte_t pte = uvpt[pn];

// 如果页表项带有 PTE_SHARE 标志，直接原样映射，不需要设置 OW
if (pte & PTE_SHARE) {
    r = sys_page_map(0, addr, envid, addr, pte & PTE_SYSCALL);
    if (r < 0) {
        panic("duppage: sys_page_map PTE_SHARE failed: %e", r);
    }
    return 0;
}
...
```

然后是 spawn 中的 copy_shared_pages，这个函数是在 spawn 创建完子进程环境后调用的。我们需要像 fork 那样遍历整个用户地址空间，如果发现哪个页表项带了 PTE_SHARE，就映射给子进程。别忘了先检查页目录项和页表项存不存在。

```c
static int
copy_shared_pages(envid_t child)
{
	// LAB 5: Your code here.
	uint32_t addr;
	int r;

	for (addr = 0; addr < UTOP; addr += PGSIZE) {
		if ((uvpd[PDX(addr)] & PTE_P) && 
		    (uvpt[PGNUM(addr)] & PTE_P) && 
		    (uvpt[PGNUM(addr)] & PTE_SHARE)) {
			
			r = sys_page_map(0, (void *)addr, child, (void *)addr, uvpt[PGNUM(addr)] & PTE_SYSCALL);
			if (r < 0) {
				return r;
			}
		}
	}
	
	return 0;
}
```

make run-testpteshare 没问题

![[MIT 6.828 Lab 5] 6](img\[MIT 6.828 Lab 5] 6.png)

## The keyboard interface

为了让 shell 工作，我们需要一种向它输入内容的方法。QEMU 一直在显示我们写入 CGA 显示器和串口的输出，但到目前为止，我们只在内核监视器中接收过输入。在 QEMU 中，在图形窗口中输入的内容会作为来自键盘的输入传给 JOS，而在控制台中输入的内容会作为串口上的字符出现。`kern/console.c` 已经包含键盘和串口驱动，这些驱动自 Lab 1 以来一直被内核监视器使用，但现在你需要把它们连接到系统的其余部分。

### Exercise 9

> 在你的 `kern/trap.c` 中，调用 `kbd_intr` 来处理陷阱 `IRQ_OFFSET+IRQ_KBD`，并调用 `serial_intr` 来处理陷阱 `IRQ_OFFSET+IRQ_SERIAL`。
>
> 我们已经在 `lib/console.c` 中为你实现了控制台输入/输出文件类型。`kbd_intr` 和 `serial_intr` 会用最近读取的输入填充一个缓冲区，而控制台文件类型会从该缓冲区中取走数据（除非用户重定向它们，否则控制台文件类型默认用于 stdin/stdout）。
>
> 通过运行 `make run-testkbd` 并输入几行来测试你的代码。系统应该在你完成每一行时把你的行回显给你。如果你同时拥有控制台和图形窗口，请尝试在两者中输入。

这里我们要把键盘和串口的中断接入系统的事件分发机制中，这样运行在用户态的 Shell 才能真正获取我们输入的指令。

回忆一下，在 x86 架构里，外部硬件设备产生的中断会被可编程中断控制器统一收集。为了防止这些硬件中断号和 CPU 内部自带的异常号撞车，所以之前我们在初始化 PIC 的时候，给所有的硬件中断号统一加上了一个偏移量 IRQ_OFFSET，值为 32。

也就是说，当键盘按下时，硬件发出的是 IRQ_KBD（IRQ 1），但反映到我们代码里的 tf->tf_trapno 时，就变成了 IRQ_OFFSET + IRQ_KBD。串口同理。

在 kern/trap.c 的 trap_dispatch 中拦截这两个特定的中断号，然后分别调用内核的设备驱动读取函数 kbd_intr() 和 serial_intr()。它们会把键盘敲击的数据读出来，放到终端的缓冲区里。

```c
// 键盘硬件中断
if (tf->tf_trapno == IRQ_OFFSET + IRQ_KBD) {
    kbd_intr();
    return;
}

// 串口硬件中断
if (tf->tf_trapno == IRQ_OFFSET + IRQ_SERIAL) {
    serial_intr();
    return;
}
```

现在 make run-testkbd，可以看见没问题，但是没有处理 backspace 和方向键等，敲这些健的话输入的是它们对应的特殊字符。

![[MIT 6.828 Lab 5] 7](img\[MIT 6.828 Lab 5] 7.png)

## Shell

运行 `make run-icode` 或 `make run-icode-nox`。这会运行你的内核并启动 `user/icode`。`icode` 会 exec `init`，后者将控制台设置为文件描述符 0 和 1（标准输入和标准输出）。然后它会 spawn `sh`，也就是 shell。你应该能够运行以下命令：

```text
	echo hello world | cat
	cat lorem |cat
	cat lorem |num
	cat lorem |num |num |num |num |num
	lsfd
```

请注意，用户库例程 `cprintf` 会直接打印到控制台，而不使用文件描述符代码。这对调试很好，但不适合把输出通过管道传给其他程序。要把输出打印到某个特定文件描述符（例如 1，即标准输出），请使用 `fprintf(1, "...", ...)`。`printf("...", ...)` 是打印到 FD 1 的简写。参见 `user/lsfd.c` 中的示例。

### Exercise 10

> shell 不支持 I/O 重定向。如果能够运行 `sh <script`，而不是像上面那样手动输入脚本中的所有命令，将会很方便。为 `user/sh.c` 添加 `<` 的 I/O 重定向。
>
> 通过在你的 shell 中输入 `sh <script` 来测试你的实现。
>
> 运行 `make run-testshell` 来测试你的 shell。`testshell` 只是把上面的命令（也可在 `fs/testshell.sh` 中找到）输入给 shell，然后检查输出是否与 `fs/testshell.key` 匹配。

实际上这个 shell 比弄 c2 的时候自己从头写 shell 简单的多。

在 UNIX 中，我们可以认为一切皆文件。 对于任何一个进程，0 号文件描述符永远代表标准输入，1 号代表标准输出。 平时，0 号和 1 号描述符是指向控制台的。当我们输入 `< script` 时，我们希望把标准输入重定向到 script 这个文件上。

所以，我们只需要以只读模式打开用户指定的文件，调用 dup(fd, 0) ，这个系统调用会把刚才打开的文件的状态复制到 0 号描述符上。这样一来，后续程序再去读 stdin (0) 时，实际上读的就是这个文件了。由于 0 号已经接管了，在用完之后把刚才打开的原 fd 给 close 掉即可。

在 user/sh.c 中的`case '<':`后面写就行，实际上可以照着`case '>':`写，输入重定向跟输出重定向的逻辑是一样的

```c
// LAB 5: Your code here.
// 以只读方式打开
if ((fd = open(t, O_RDONLY)) < 0) {
    cprintf("open %s for read: %e", t, fd);
    exit();
}
// 将打开的文件描述符复制到 0
if (fd != 0) {
    dup(fd, 0);
    // 关闭原来的描述符
    close(fd);
}
// panic("< redirection not implemented");
break;
```

make run-testshell

![[MIT 6.828 Lab 5] 8](img\[MIT 6.828 Lab 5] 8.png)

可以看见 shell ran correctly

---

make grade

![[MIT 6.828 Lab 5] 9](img\[MIT 6.828 Lab 5] 9.png)



可以感觉到这个 Lab 实际上没啥难度，之前的 Lab 已经把框架搭好了，这个 Lab 很多都是复用之前的思维，水到渠成了。
