本实验中将为 JOS 编写内存管理代码，分为两个部分。一个是内核的物理内存分配器，以 4096 字节为单位（页）。一个是虚拟内存。

# Getting started

教我们怎么把 lab2 的分支 merge 进 lab1。

新增文件：

- inc/memlayout.h
- kern/pmap.c
- kern/pmap.h
- kern/kclock.h
- kern/kclock.c

特别注意 memlayout.h 和 pmap.h，实验中会用到其中的很多定义

# Part 1: Physical Page Management

操作系统必须跟踪物理内存中哪些部分是空闲的，哪些部分正在使用。JOS 以页为单位管理 PC 的物理内存，以便利用内存管理单元来映射和保护每一块已分配的内存。

## Exercise 1

现在你要编写物理页分配器。它使用一个链表来跟踪哪些页面是空闲的，链表中包含 struct PageInfo 对象（与 xv6 不同，这些对象本身并不嵌入到空闲页面中），每个对象对应一个物理页。你必须先编写物理页分配器，才能编写虚拟内存实现的其余部分，因为你的页表管理代码需要分配物理内存来存储页表。

> 在文件 `kern/pmap.c` 中，您必须实现以下函数的代码（可能按给定的顺序）。
>
> `boot_alloc()`
>
> `mem_init()` （仅到调用 `check_page_free_list(1)` 为止）
>
> `page_init()`
>
> `page_alloc()`
>
> `page_free()`
>
> `check_page_free_list()` 和 `check_page_alloc()` 会测试你的物理页面分配器。你应该启动 JOS 系统，并查看 `check_page_alloc()` 函数是否正常工作。 报告成功。请修改代码使其通过测试。您可能会发现问题所在。 添加自己的 `assert()` 语句来验证你的假设是否正确会很有帮助。

本次实验以及所有 6.828 实验都需要你进行一些探索性的工作，才能弄清楚具体需要做什么。本次作业并未详细描述你需要添加到 JOS 中的代码。请仔细查找 JOS 源代码中需要修改的部分的注释；这些注释通常包含规范和提示。你还需要查看 JOS 的相关部分、Intel 手册，以及你可能用过的 6.004 或 6.033 课程笔记。

---

终于要开始自己写东西了。之前的 Lab 1，我们能启动内核，执行指令，但是不能管理内存。Exercise 1 就是开始写内存分配的东西了。

### boot_alloc

按照 Exercise 给的顺序，我们先实现 `boot_alloc`。这是一个分配器。根据注释，我们可以实现一个只增不减的分配器。

在内核刚启动时，我们还没有建立起完善的页表，但是，我们又需要分配内存来存放“页表目录”本身，以及用来管理物理页的结构体数组。所以，我们得写一个简易的临时分配器，它不需要支持 free 等操作，只需要不停分配未使用的内存，这就是 `boot_alloc` 的作用。

系统启动时，Linker 会在内核代码的末尾定义一个 end，这个 end 之后的内存目前是没有人在用的。我们需要去维护一个 nextfree，每次申请 n 字节，就把 nextfree 往后移 n 字节。这里，我们得保证内存对齐到 4KB （inc/mmu.h: PGSIZE 4096）

```c
if (n == 0)
    return nextfree;

result = nextfree;
nextfree = ROUNDUP(nextfree + n, PGSIZE);

// 溢出检查，KERNBASE 是内核虚拟地址的起始点，npages * PGSIZE 是物理内存总大小
if ((uint32_t)nextfree - KERNBASE > npages * PGSIZE)
    panic("boot_alloc: out of memory");

return result;
```

kern/kernel.ld 内可以看见 end 的定义

![[MIT 6.828] Lab 2 1](img\[MIT 6.828] Lab 2 1.png)

### mem_init

实际上这个函数才是 Exercise 1 的入口点。

根据注释的指示，我们这里只需要分配一个数组来管理所有的物理页。JOS 使用 `struct PageInfo` 结构体来描述每一个物理页的状态，如果有 `npages` 个物理页，我们就需要分配一个大小为 `npages` 的 `struct PageInfo` 数组。

```c
//////////////////////////////////////////////////////////////////////
// Allocate an array of npages 'struct PageInfo's and store it in 'pages'.
// Your code goes here:

uint32_t pages_size = npages * sizeof(struct PageInfo);
pages = (struct PageInfo *) boot_alloc(pages_size);
memset(pages, 0, pages_size);
```

### page_init

现在我们有 pages 数组了，我们需要根据物理内存的实际布局，分别处理空闲页和已占用的页。

这里代码注释给我们解释得很清楚了。

首先是 Page 0 (0x0000 - 0x1000)，这一块是保留给 实模式中断向量表 Real-Mode IDT 和 BIOS 结构体的。

然后，剩余的 Base Memory (0x1000 - 0xA0000) 是空闲的；I/O Hole (0xA0000 - 0x100000)永不分配。

扩展内存 (0x100000 - ...) 有的空闲，有的在使用。内核代码就加载在这里的起始位置，我们得搞清楚内核占用的部分从 0x100000 开始到哪里结束。

那我们怎么内核占用到哪结束呢？之前的 `boot_alloc` 就能派上用场了。我们专门为 `boot_alloc(0)` 预留了特判，此时我们调用它即可指向下一个可用空间的起始地址，也就知道内核占用到哪结束了。

```c
size_t i;
page_free_list = NULL;
// boot_alloc 返回的是虚拟地址 KV A，必须转为物理地址 PA
physaddr_t first_free_pa = PADDR(boot_alloc(0));

for (i = 0; i < npages; i++) {
    // 计算当前页的物理地址
    physaddr_t pa = i * PGSIZE;
    // 第0页：保留
    if (i == 0) {
        pages[i].pp_ref = 1;
        pages[i].pp_link = NULL;
    }
    // IO Hole (IOPHYSMEM 到 EXTPHYSMEM 之间)：保留 
    else if (pa >= IOPHYSMEM && pa < EXTPHYSMEM) {
        pages[i].pp_ref = 1;
        pages[i].pp_link = NULL;
    }
    // 内核代码区 (EXTPHYSMEM 到 first_free_pa 之间)：保留 
    else if (pa >= EXTPHYSMEM && pa < first_free_pa) {
        // 内核代码、全局变量、页目录表、pages数组本身 占用的区域
        pages[i].pp_ref = 1;
        pages[i].pp_link = NULL;
    }
    else {
        // 剩下的是真正的空闲页
        pages[i].pp_ref = 0;
        pages[i].pp_link = page_free_list;
        page_free_list = &pages[i];
    }
}
```

### page_alloc

在 `mem_init` 中，接下来该 `check_page_free_list`，这里其实是检查 `page_free_list` 中的空闲页是否合法，这之后就是 `check_page_alloc`，这个函数的功能是检查 `page_alloc`，`page_free` 两个函数是否能够正确运行。

现在 `page_free_list` 里全是空闲的 `PageInfo` 结构体。用`page_alloc`分配一个页，实际上就是从链表头取下一个节点。另外注意，在开启分页机制后，CPU 里的指令操作的全部是虚拟地址。如果直接 `memset(phys_addr, ...)`，CPU 会把这个物理地址当成虚拟地址去查页表，那就 Page Fault 了。

```c
// Fill this function in
struct PageInfo *pp;
if (page_free_list == NULL)
    return NULL;

pp = page_free_list;
page_free_list = pp->pp_link;
pp->pp_link = NULL;

// 处理 ALLOC_ZERO 标志，如果设置了这个标志，我们需要把分配到的物理页清零
if (alloc_flags & ALLOC_ZERO) {
    // memset 操作的是虚拟地址，pp 是结构体指针，page2pa(pp) 是物理地址
    // 必须用 page2kva 将其转换为内核虚拟地址
    memset(page2kva(pp), 0, PGSIZE);
}

return pp;
```

### page_free

就是把这个页再放回 `page_free_list`

```c
if (pp->pp_ref != 0)
    panic("page_free: pp->pp_ref is nonzero");
if (pp->pp_link != NULL)
    panic("page_free: pp->pp_link is not NULL");
pp->pp_link = page_free_list;
page_free_list = pp;
```

写完后发现 check 函数没有 panic 也没有 succeeded，做完 Exercise 4 后又排查了一万年发现是 `kern/init.c` 的 `i386_init`里 `mem_init()` 没了。

然后发现之前 merge lab 1 和 lab 2 的时候我 merge 错了，草

把整个 repo 全部重新搞了一遍后正常了。

现在就能看见 mem_init 里的情况了。记得注释掉第一个 panic，测试完了再注释回去

![[MIT 6.828] Lab 2 2](img\[MIT 6.828] Lab 2 2.png)

可以看见两个 check 都 succeeded 了，剩下的 panic 是之后的练习的事情了。

# Part 2: Virtual Memory

在开始之前，请先熟悉 x86 的保护模式内存管理架构：即分段和页面转换。

## Exercise 2

就是学页表和虚拟内存的相关知识

---

## Virtual, Linear, and Physical Addresses

在 x86 中，**虚拟地址**(Virtual Address) 由**段选择子**(Segment Selector) 和**段内偏移**(Segment Offset)组成。**线性地址**(Linear Address)是经过段转换但尚未经过页转换后得到的地址。**物理地址**是经过段转换和页转换后最终得到的地址，也是最终通过总线发送到 RAM 的地址。

![[MIT 6.828] Lab 2 3](img\[MIT 6.828] Lab 2 3.png)

C 指针是虚拟地址的“偏移量”部分。在 `boot/boot.S` 中，我们引入了一个全局描述符表 (GDT)，它通过将所有段基址设置为 0 并将限制设置为 `0xffffffff`，有效地禁用了段转换。因此，“选择子”没有影响，线性地址总是等于虚拟地址的偏移量。在 Lab 3 中，我们将需要更多地与分段交互以设置特权级，但对于内存转换，我们可以在整个 JOS 实验中忽略分段，只关注页转换。

回顾一下，在 Lab 1 的第 3 部分中，我们引入了一个简单的页表，以便内核可以在其链接地址 `0xf0100000` 处运行，尽管它实际上加载在 ROM BIOS 物理内存 `0x00100000` 之上。这个页表只映射了 4MB 的内存。在本实验中我们将为 JOS 设置的虚拟地址空间布局中，我们将扩展它以映射从虚拟地址 `0xf0000000` 开始的前 256MB 物理内存，并映射虚拟地址空间的其他一些区域。

### Exercise 3

> 虽然 GDB 只能通过虚拟地址访问 QEMU 的内存，但在设置虚拟内存时能够检查物理内存通常很有用。复习实验工具指南中的 QEMU 监视器命令，特别是 `xp` 命令，它允许你检查物理内存。要访问 QEMU 监视器，请在终端中按 `Ctrl-a c`（同样的绑定可返回串行控制台）。
>
> 使用 QEMU 监视器中的 `xp` 命令和 GDB 中的 `x` 命令检查相应物理地址和虚拟地址的内存，并确保你看到相同的数据。
>
> 我们修补过的 QEMU 版本提供了一个 `info pg` 命令，这也可能很有用：它显示当前页表的紧凑但详细的表示，包括所有映射的内存范围、权限和标志。原版 QEMU 也提供了一个 `info mem` 命令，显示哪些虚拟地址范围被映射以及具有什么权限的概览。

先看 gdb 的，make qemu-gdb 然后新终端 make gdb，c 让内核跑起来，然后 ctrl c 中断

```shell
(gdb) x/20x 0xf0100000
0xf0100000 <_start+4026531828>: 0x1badb002      0x00000000      0xe4524ffe      0x7205c766
0xf0100010 <entry+4>:   0x34000004      0x3000b812      0x220f0011      0xc0200fd8
0xf0100020 <entry+20>:  0x0100010d      0xc0220f80      0x10002fb8      0xbde0fff0
0xf0100030 <relocated+1>:       0x00000000      0x111000bc      0x0068e8f0      0xfeeb0000
0xf0100040 <test_backtrace>:    0x56e58955      0x0183e853      0xc3810000      0x000122be
```

然后继续 c，在 make qemu-gdb 的窗口 ctrl a, c，打开 qemu monitor

```shell
(qemu) x/16xw 0xf0100000
f0100000: 0x1badb002 0x00000000 0xe4524ffe 0x7205c766
f0100010: 0x34000004 0x3000b812 0x220f0011 0xc0200fd8
f0100020: 0x0100010d 0xc0220f80 0x10002fb8 0xbde0fff0
f0100030: 0x00000000 0x111000bc 0x0068e8f0 0xfeeb0000
(qemu) x/16xw 0x100000
00100000: 0x1badb002 0x00000000 0xe4524ffe 0x7205c766
00100010: 0x34000004 0x3000b812 0x220f0011 0xc0200fd8
00100020: 0x0100010d 0xc0220f80 0x10002fb8 0xbde0fff0
00100030: 0x00000000 0x111000bc 0x0068e8f0 0xfeeb0000
```

可以看出来是一样的数据，也就是 虚拟地址 f0100000 映射物理地址 100000 的数据

---

从在 CPU 上执行的代码来看，一旦我们进入保护模式（我们在 `boot/boot.S` 中首先做的事情），就无法直接使用线性或物理地址。所有内存引用都被解释为虚拟地址并由 MMU 转换，这意味着 C 中的所有指针都是虚拟地址。

JOS 内核经常需要将地址作为不透明值或整数进行操作，而不解引用它们，例如在物理内存分配器中。有时这些是虚拟地址，有时它们是物理地址。为了帮助记录代码，JOS 源代码区分了这两种情况：类型 `uintptr_t` 表示不透明的虚拟地址，而 `physaddr_t` 表示物理地址。这两种类型实际上都只是 32 位整数 (`uint32_t`) 的同义词，所以编译器不会阻止你将一种类型赋值给另一种类型！由于它们是整数类型（不是指针），如果你试图解引用它们，编译器会报错。

JOS 内核可以通过先将 `uintptr_t` 转换为指针类型来解引用它。相比之下，内核无法合理地解引用物理地址，因为 MMU 会转换所有内存引用。如果你将 `physaddr_t` 转换为指针并解引用它，你也许能够加载和存储到结果地址（硬件将其解释为虚拟地址），但你可能得不到你想要的内存位置。

总结：

| **C 类型**   | **地址类型** |
| ------------ | ------------ |
| `T*`         | 虚拟         |
| `uintptr_t`  | 虚拟         |
| `physaddr_t` | 物理         |

---

### Question

>假设下面的 JOS 内核代码是正确的，变量 `x` 应该是什么类型，`uintptr_t` 还是 `physaddr_t`？
>
>```c
>mystery_t x;
>char* value = return_a_pointer();
>*value = 10;
>x = (mystery_t) value;
>```

显然应该是 uintptr_t。

再回顾一下，在开启了分页机制的 x86 保护模式下，CPU 执行的任何指令中涉及的内存地址，都会被 MMU当作虚拟地址去查页表，然后转换成物理地址。`*value = 10 ` 时 CPU 就会向 `value` 指向的地址写入数据。显然，CPU 会把它当做一个虚拟地址去查页表。

换句话说，所有能被解引用的指针，都必须是有效的虚拟地址。

---

JOS 内核有时需要读取或修改它只知道物理地址的内存。例如，向页表添加映射可能需要分配物理内存来存储页目录，然后初始化该内存。然而，内核无法绕过虚拟地址转换，因此无法直接加载和存储到物理地址。JOS 将从物理地址 0 开始的所有物理内存重新映射到虚拟地址 `0xf0000000` 的一个原因，就是为了帮助内核读取和写入它只知道物理地址的内存。为了将物理地址转换为内核实际可以读取和写入的虚拟地址，内核必须将 `0xf0000000` 加到物理地址上，以在重新映射区域中找到其对应的虚拟地址。你应该使用 `KADDR(pa)` 来执行该加法。

JOS 内核有时还需要能够根据存储内核数据结构的内存的虚拟地址找到物理地址。内核全局变量和由 `boot_alloc()` 分配的内存位于内核加载的区域，从 `0xf0000000` 开始，这正是我们将所有物理内存映射到的区域。因此，要将该区域中的虚拟地址转换为物理地址，内核只需减去 `0xf0000000`。你应该使用 `PADDR(va)` 来执行该减法。

## Reference counting

在未来的实验中，你会经常遇到同一个物理页同时映射在多个虚拟地址（或在多个环境的地址空间中）的情况。你将在与物理页对应的 `struct PageInfo` 的 `pp_ref` 字段中保持对每个物理页的引用计数。当一个物理页的计数归零时，该页可以被释放，因为它不再被使用了。通常，这个计数应该等于该物理页在所有页表中出现在 `UTOP` 下方的次数（`UTOP` 上方的映射主要由内核在启动时设置，不应被释放，所以不需要对它们进行引用计数）。我们还将使用它来跟踪我们保留的指向页目录页的指针数量，进而跟踪页目录对页表页的引用数量。

使用 `page_alloc` 时要小心。它返回的页的引用计数始终为 0，因此一旦你对返回的页做了一些事情（比如将其插入页表），就应该增加 `pp_ref`。有时这由其他函数处理（例如 `page_insert`），有时调用 `page_alloc` 的函数必须直接处理。

## Page Table Management

现在你将编写一组例程来管理页表：插入和删除线性到物理的映射，并在需要时创建页表页。

### Exercise 4

>在文件 `kern/pmap.c` 中，你必须实现以下函数的代码。
>
>`pgdir_walk()`
>
>`boot_map_region()`
>
>`page_lookup()`
>
>`page_remove()`
>
>`page_insert()`
>
>`check_page()` 从 `mem_init()` 调用，用于测试你的页表管理例程。在继续之前，你应该确保它报告成功。

根据函数里的注释 Hint，完成即可

#### pgdir_walk

给定一个虚拟地址 `va`，找到它对应的 PTE 在哪里。如果中间的页表不存在，根据 `create` 标志决定是否创建。

```c
// 获取页目录项的索引
uint32_t pdx = PDX(va);

// 获取页目录项指针
pte_t *pde = &pgdir[pdx];

// 检查页表是否存在
pte_t *pgtable_va;
struct PageInfo *pp;

if (*pde & PTE_P) {
    // 页表已经存在
    pgtable_va = (pte_t *) KADDR(PTE_ADDR(*pde));
}
else {
    // 页表不存在
    if (!create) {
        return NULL;
    }

    // 分配一个新的物理页作为页表
    if ((pp = page_alloc(ALLOC_ZERO)) == NULL) {
        return NULL;
    }

    pp->pp_ref++;

    // 获取新页表的虚拟地址
    pgtable_va = (pte_t *) page2kva(pp);

    // 更新页目录项
    // 权限：PTE_P (Present), PTE_W (Writable), PTE_U (User accessible)
    *pde = page2pa(pp) | PTE_P | PTE_W | PTE_U;
}

// pgtable_va 是页表的基址，用 PTX(va) 索引
return &pgtable_va[PTX(va)];
```

#### boot_map_region

把虚拟地址 `[va, va+size)` 映射到物理地址 `[pa, pa+size)`。 这个函数只在启动时用，不需要考虑引用计数，也不用考虑页表被释放的情况。

```c
size_t num_pages = (size + PGSIZE - 1) / PGSIZE;
    
size_t i;
for (i = 0; i < num_pages; i++) {
    // 寻找 PTE，create=1 
    pte_t *pte = pgdir_walk(pgdir, (void *)(va + i * PGSIZE), 1);

    if (pte == NULL) {
        panic("boot_map_region: out of memory");
    }

    // 修改 PTE，填入当前页对应的物理地址 (pa + i * PGSIZE) 和权限
    *pte = (pa + i * PGSIZE) | perm | PTE_P;
}
```

#### page_lookup

给一个虚拟地址，查询对应的物理页结构体 `PageInfo`。如果想要 PTE 的地址，也顺便存进去。

注意检查`!(*pte & PTE_P)`

```c
pte_t *pte = pgdir_walk(pgdir, va, 0);

// 如果页表不存在，或者 PTE 标记为不存在
if (pte == NULL || !(*pte & PTE_P)) {
    return NULL;
}

if (pte_store) {
    *pte_store = pte;
}

// 将 PTE 中的物理地址转为 PageInfo 结构体
return pa2page(PTE_ADDR(*pte));
```

#### page_insert

将物理页 `pp` 映射到虚拟地址 `va`。这里得注意，如果 `va` 已经映射了原来的物理页，要先移除；还得考虑 `va` 本来就映射到了 `pp`，也就是自己映射自己的情况。

```c
pte_t *pte = pgdir_walk(pgdir, va, 1);

if (pte == NULL) {
    return -E_NO_MEM; // 返回负数错误码
}

// 提前增加引用计数，处理 "映射到同一个物理页" 的情况
pp->pp_ref++;

// 如果该 va 之前已经映射了页面，先移除它
if (*pte & PTE_P) {
    page_remove(pgdir, va);
}

// 更新 PTE
// page2pa(pp) 得到物理地址，perm 是低 12 位的权限标记，PTE_P 必须置位
*pte = page2pa(pp) | perm | PTE_P;

return 0;
```

#### page_remove

取消映射，如果物理页没人在用了，就释放它。

修改了页表后，CPU 的 TLB 里可能还存着旧的映射关系。必须调用 `invlpg` 指令（封装在 `tlb_invalidate` 中）来强制 CPU 刷新缓存，否则 CPU 会访问到错误的物理地址。

```c
pte_t *pte;
struct PageInfo *pp = page_lookup(pgdir, va, &pte);
if (pp == NULL) {
    return;
}
page_decref(pp);
*pte = 0;

// 修改了页表，必须通知 CPU 的缓存失效
tlb_invalidate(pgdir, va);
```

现在 make qemu 看看

![[MIT 6.828] Lab 2 4](img\[MIT 6.828] Lab 2 4.png)

可以看见 `check_page()` 通过了

# Part 3: Kernel Address Space

JOS 将处理器的 32 位线性地址空间分为两部分。用户环境（进程）将控制下半部分的布局和内容，而内核始终保持对上半部分的完全控制。分界线由 `inc/memlayout.h` 中的符号 `ULIM` 任意定义，为内核保留了大约 256MB 的虚拟地址空间。这也解释了为什么我们在 Lab 1 中需要给内核这样一个高的链接地址：否则就没有足够的空间在内核的虚拟地址空间中同时在它下面映射一个用户环境。

你会发现参考 `inc/memlayout.h` 中的 JOS 内存布局图对于本部分和后续实验都很有帮助。

## Permissions and Fault Isolation

由于内核和用户内存都存在于每个环境的地址空间中，我们将不得不使用 x86 页表中的权限位来允许用户代码仅访问地址空间的用户部分。否则，用户代码中的错误可能会覆盖内核数据，导致崩溃或更微妙的故障；用户代码也可能窃取其他环境的私有数据。注意，可写权限位 `PTE_W` 同时影响用户和内核代码。

用户环境将没有权限访问 `ULIM` 之上的任何内存，而内核将能够读取和写入此内存。对于地址范围 `[UTOP, ULIM)`，内核和用户环境具有相同的权限：它们可以读取但不能写入此地址范围。此地址范围用于将某些内核数据结构以只读方式暴露给用户环境。最后，`UTOP` 以下的地址空间供用户环境使用；用户环境将设置访问此内存的权限。

## Initializing the Kernel Address Space

现在你将设置 `UTOP` 之上的地址空间：地址空间的内核部分。`inc/memlayout.h` 显示了你应该使用的布局。你将使用你刚刚编写的函数来设置适当的线性到物理映射。

### Exercise 5

> 在 `mem_init()` 中调用 `check_page()` 之后填写缺失的代码。
>
> 你的代码现在应该通过 `check_kern_pgdir()` 和 `check_page_installed_pgdir()` 检查。

看 memlayout.h，分成了 ULIM 和 UTOP。注释中的任务其实就是把 ULIM 映射到物理内存上，这一块是用户只读的。

我们之前设了一个巨大的 pages 数组，现在我们得把它映射到虚拟地址 UPAGES。这里要注意以 PGSIZE 对齐，所以数组大小得向上取整。另外还得限死用户态权限。

所以第一段注释的代码就是

```c
size_t pages_size = ROUNDUP(npages * sizeof(struct PageInfo), PGSIZE);
boot_map_region(kern_pgdir, UPAGES, pages_size, PADDR(pages), PTE_U | PTE_P);
```

然后编译的时候就发现跟 Exercise 1 的时候写的 mem_init 的定义撞了。把之前的 pages_size 统一成 size_t，这里就不再重新定义就行。

第二段注释是映射内核栈。内核栈的最高地址是 KSTACKTOP，memlayout.h 里可以看见分配给栈的总虚拟空间是 PTSIZE（4 MB），但实际上我们只有 KSTKSIZE（32 KB）的空间去存放栈数据，这块物理内存的基址就是 bootstack。

剩下的空间其实就是保护页了，我们故意不映射它，以便 Page Fault 的触发。

那么第二段注释的代码就是

```c
boot_map_region(kern_pgdir, KSTACKTOP - KSTKSIZE, KSTKSIZE, PADDR(bootstack), PTE_W | PTE_P);
```

第三段注释要求直接映射整个物理内存。相当于把虚拟空间顶部的 256 MB 全部映射到物理内存的 0x00000000  到  0x0FFFFFFF 

这里注意，2^32 - KERNBASE 这个大小不能直接这样写，因为 2^32 在 32 位系统下会溢出。所以写成 0xFFFFFFFF - KERNBASE + 1

```c
uint32_t mem_size = 0xFFFFFFFF - KERNBASE + 1;
boot_map_region(kern_pgdir, KERNBASE, mem_size, 0, PTE_W | PTE_P);
```

现在就可以编译通过了。

![[MIT 6.828] Lab 2 5](img\[MIT 6.828] Lab 2 5.png)

另外，我看见很多参考博客里都没有把 PTE_P 写入权限，于是好奇地试了一下删掉会不会 check 不通过，结果还是通过了。

检查了一下 check_kern_pgdir，明明是有 ` assert(pgdir[i] & PTE_P); ` 检查的

```c
// check PDE permissions
for (i = 0; i < NPDENTRIES; i++) {
    switch (i) {
        case PDX(UVPT):
        case PDX(KSTACKTOP-1):
        case PDX(UPAGES):
            assert(pgdir[i] & PTE_P);
            break;
        default:
            if (i >= PDX(KERNBASE)) {
                assert(pgdir[i] & PTE_P);
                assert(pgdir[i] & PTE_W);
            } else
                assert(pgdir[i] == 0);
            break;
    }
}
```

然后最后发现是几天前的自己写的 boot_map_region 里 | PTE_P 了，而且是注释明确要求了的，蛤蛤

```c
// Use permission bits perm|PTE_P for the entries.
...
*pte = (pa + i * PGSIZE) | perm | PTE_P;
```

### Question

1. > 此时页目录中的哪些条目（行）已被填充？它们映射什么地址，指向哪里？换句话说，尽可能填写下表
   >
   > ...

| **Entry**      | **Base Virtual Address**      | **Points to (logically):**                          |
| -------------- | ----------------------------- | --------------------------------------------------- |
| **1023**       | 0xEF400000 (UVPT)             | 页目录表自身，这是用于实现虚拟页表的递归映射。      |
| **960 - 1022** | 0xF0000000 (KERNBASE)         | 物理内存 [0, 256MB)，对应映射整个物理内存的代码。   |
| **959**        | 0xEF800000 (KSTACKTOP-PTSIZE) | 内核栈，对应 bootstack 所在的物理页。               |
| **957**        | 0xEF000000 (UPAGES)           | PageInfo 结构体数组，用于让用户态只读查询内存状态。 |
| **0 - 956**    | 0x00000000 到 0xEEFFFFFF      | 暂未映射，之后会提供给用户态环境使用                |

2. > 我们将内核和用户环境放在同一个地址空间中。为什么用户程序无法读取或写入内核的内存？什么特定的机制保护了内核内存？

   PTE_U 标志位，我们在实现 mem_init 时都没有加上 PTE_U 权限。这样 CPU在 Ring 3 时访问没有 PTE_U 标志的页时，MMU  就会抛出 Page Fault

3. > 这个操作系统可以支持的最大物理内存量是多少？为什么？

   2 GB。mmu.h 中，PTSIZE = (PGSIZE * NPTENTRIES) = 4096 * 1024，即 PTSIZE = 4 MB，而 memlayout.h中分给 UPAGES 的虚拟空间大小就是 PTSIZE。

   一个 struct PageInfo 是 8 字节，也就是只能装下 4 MB / 8 B 个 PageInfo，一个 PageInfo 代表 4KB 的物理页，那就是 4 MB / 8 B * 4 KB = 2 GB。这就是寻址极限了。就算到了 32 位系统的极限 4 GB，JOS 也只认得出 2 GB

   

4. > 如果我们实际上拥有最大数量的物理内存，管理内存的空间开销是多少？这些开销是如何构成的？

   pages 数组就得占 4 MB；

   页目录表 1 个，4 KB；

   二级页表，一个页表映射 4 MB，2 GB 就要 512 个页表。1 个页表 4 KB，一共就是 2 MB

   加起来大概 6 MB
   
5. > 回顾 `kern/entry.S` 和 `kern/entrypgdir.c` 中的页表设置。在我们开启分页后，EIP 仍然是一个很小的数字（略高于 1MB）。我们在什么时候转换到 KERNBASE 以上的 EIP 运行？是什么使我们能够在开启分页和开始在 KERNBASE 以上的 EIP 运行之间继续在低 EIP 执行？为什么这种转换是必要的？

   1. 什么时候？

      entry.S 的 `jmp	*%eax`。

   2. 为什么还能继续执行？

      ```c
      pde_t entry_pgdir[NPDENTRIES] = {
      	// Map VA's [0, 4MB) to PA's [0, 4MB)
      	[0]
      		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
      	// Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
      	[KERNBASE>>PDXSHIFT]
      		= ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
      };
      ```

      在 entrypgdir.c 中可以看见这两个映射。当 entry.S 中执行了分页的那行汇编时，EIP 还在低地址，此时这两条映射就发挥作用了，CPU 依然能通过这两个映射去找到下一条指令的物理地址。

   3. 为什么是必要的？

      如果不这样做，在开启分页机制后，CPU 会去读取下一条指令，这条指令依然是低地址。如果此时没有映射，就会 Page Fault。

### Challenge 1

> 我们消耗了许多物理页来保存 KERNBASE 映射的页表。使用页目录条目中的 `PTE_PS` ("Page Size") 位做一个更节省空间的工作。这个位在原始的 80386 中不受支持，但在较新的 x86 处理器上受支持。因此，你必须参考当前 Intel 手册的第 3 卷。确保你的内核设计仅在支持该优化的处理器上使用它！

为了把 256 MB 的物理内存映射到 KERNBASE，我们需要 256 MB / 4 KB = 65536 个 PTE。一个页表可以装 1024 个 PTE，所以我们需要消耗 64 个物理页（256 KB）。

既然这 256 MB 内存是连续映射的，那么我们可以直接用 4 MB 的页去搞定，并且只需要修改页目录表的 64 个 PDE，甚至不需要额外分配页表了。

现在我们就该替换一下之前映射 KERNBASE 的代码了

```c
// CHALLENGE 1:

// uint32_t mem_size = 0xFFFFFFFF - KERNBASE + 1;
// boot_map_region(kern_pgdir, KERNBASE, mem_size, 0, PTE_W | PTE_P);

uint32_t eax, ebx, ecx, edx;
cpuid(1, &eax, &ebx, &ecx, &edx);
if (edx & (1 << 3)) { // edx 的第三位是 PSE 
    cprintf("CPU Supports PSE.\n");
    uint32_t cr4 = rcr4() | CR4_PSE;
    lcr4(cr4);

    size_t i;
    for (i = 0; i < 64; ++i) {
        physaddr_t pa = i * PTSIZE;
        kern_pgdir[PDX(KERNBASE) + i] = pa | PTE_P | PTE_W | PTE_PS;
    }
}
else {
    cprintf("CPU Does Not Support PSE.\n");
}
```

然后

![[MIT 6.828] Lab 2 6](img\[MIT 6.828] Lab 2 6.png)

居然 kernel panic 了，然后发现其实是 check_va2pa 校验函数不能识别大页，直接对大页解引用，当然会导致 assertion 错误了。稍微修改一下 check_va2pa，新增一个大页校验。如果这是一个 4 MB 大页，那就该物理地址 = PDE 的高 10 位（大页基址） + 虚拟地址的低 22 位（页内偏移）。

```c
static physaddr_t
check_va2pa(pde_t *pgdir, uintptr_t va)
{
	pte_t *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;

	if (*pgdir & PTE_PS) {
        return PTE_ADDR(*pgdir) | (va & 0x003FFFFF);
    }

	p = (pte_t*) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}
```

现在再 make qemu 就没问题了。

### Challenge 2

>  扩展 JOS 内核监视器，增加以下命令：
>
> - 以有用且易于阅读的格式显示适用于当前活动地址空间中特定范围的虚拟/线性地址的所有物理页映射（或缺乏映射）。例如，你可以输入 `'showmappings 0x3000 0x5000'` 来显示适用于虚拟地址 0x3000、0x4000 和 0x5000 的页面的物理页映射和相应的权限位。
> - 显式设置、清除或更改当前地址空间中任何映射的权限。
> - 给定虚拟或物理地址范围，转储内存范围的内容。确保存储转储代码在范围跨越页边界时表现正确！
> - 做任何其他你认为以后可能对调试内核有用的事情。（很有可能会有用！）

开始修改 kern/monitor.c 吧

首先是 showmappings，其核心是遍历一段虚拟地址，调用 pgdir_walk获取物理地址和权限。

```c
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 3) {
        cprintf("Usage: showmappings 0x<begin_va> 0x<end_va>\n");
        return 0;
    }

    // 转换为十六进制地址
    uintptr_t start = strtol(argv[1], NULL, 16);
    uintptr_t end = strtol(argv[2], NULL, 16);

    // 按页对齐
    start = ROUNDDOWN(start, PGSIZE);
    end = ROUNDUP(end, PGSIZE);

    cprintf("    VA       |     PA       | P W U\n");
    cprintf("-----------------------------------\n");

    for (; start <= end; start += PGSIZE) {
        pte_t *pte = pgdir_walk(kern_pgdir, (void *)start, 0);
        if (!pte || !(*pte & PTE_P)) {
            cprintf(" 0x%08x  |  <unmapped>  | - - -\n", start);
        }
		else {
            int p = (*pte & PTE_P) ? 1 : 0;
            int w = (*pte & PTE_W) ? 1 : 0;
            int u = (*pte & PTE_U) ? 1 : 0;
            cprintf(" 0x%08x  |  0x%08x  | %d %d %d\n", start, PTE_ADDR(*pte), p, w, u);
        }
        if (start >= 0xFFFFF000) break; // 防止地址溢出，导致死循环
    }
    return 0;
}
```

然后是 setm，找到该页的 PTE，然后 |= 置 1，&= ~ 置 0 即可。一定要记住，修改页表后必须刷新 TLB，使原有的 CPU 缓存失效

```c
int
mon_setm(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 4) {
        cprintf("Usage: setm 0x<va> <P|W|U> <0|1>\n");
        return 0;
    }

    uintptr_t va = strtol(argv[1], NULL, 16);
    char flag = argv[2][0];
    int val = strtol(argv[3], NULL, 10);

    pte_t *pte = pgdir_walk(kern_pgdir, (void *)va, 0);
    if (!pte || !(*pte & PTE_P)) {
        cprintf("Error: Virtual address 0x%08x is not mapped.\n", va);
        return 0;
    }

    uint32_t mask = 0;
    if (flag == 'P' || flag == 'p') mask = PTE_P;
    else if (flag == 'W' || flag == 'w') mask = PTE_W;
    else if (flag == 'U' || flag == 'u') mask = PTE_U;
    else {
        cprintf("Error: Invalid flag. Use P, W, or U.\n");
        return 0;
    }

    if (val) {
        *pte |= mask;
    }
	else {
        *pte &= ~mask;
    }

    // 修改页表后必须使原有的 CPU 缓存失效
    tlb_invalidate(kern_pgdir, (void *)va);
    cprintf("Successfully updated permission at 0x%08x.\n", va);
    return 0;
}
```

然后是 dumpm，注意专门强调了一下"确保存储转储代码在范围跨越页边界时表现正确！"那么在打印某个字节之前，首先检查它所在的页是否存在。

```c
int
mon_dumpm(int argc, char **argv, struct Trapframe *tf)
{
    if (argc != 4) {
        cprintf("Usage: dumpm <-v|-p> 0x<addr> <num_words>\n");
        return 0;
    }

    int is_virtual = (argv[1][1] == 'v');
    uintptr_t addr = strtol(argv[2], NULL, 16);
    uint32_t count = strtol(argv[3], NULL, 10);

    cprintf("Dumping %d words starting at %s 0x%08x:\n", count, is_virtual ? "VA" : "PA", addr);

    uint32_t i;
    for (i = 0; i < count; i++) {
        uintptr_t current_addr = addr + i * 4;
        uintptr_t read_va;

        if (is_virtual) {
            read_va = current_addr;
            // 检查当前地址所在的页是否映射
            pte_t *pte = pgdir_walk(kern_pgdir, (void *)ROUNDDOWN(read_va, PGSIZE), 0);
            if (!pte || !(*pte & PTE_P)) {
                cprintf("\nError: Page boundary crossed into unmapped VA: 0x%08x. Stopping.\n", read_va);
                break;
            }
        } else {
            // 物理地址转内核虚拟地址, 必须小于机器总物理内存
            if (current_addr >= npages * PGSIZE) {
                cprintf("\nError: Physical address 0x%08x out of bounds. Stopping.\n", current_addr);
                break;
            }
            read_va = (uintptr_t)KADDR(current_addr);
        }

        if (i % 4 == 0) cprintf("\n0x%08x: ", current_addr);
        
        cprintf("%08x ", *(uint32_t *)read_va);
    }
    cprintf("\n");
    return 0;
}
```

然后 make qemu，来测试一下

首先是 showmappings

![[MIT 6.828] Lab 2 7](img\[MIT 6.828] Lab 2 7.png)

内核栈是可读可写用户不可见的，没什么问题

然后 dumpm，这里主要检查一下边界保护有没有生效

![[MIT 6.828] Lab 2 8](img\[MIT 6.828] Lab 2 8.png)

最后 setm，改完再用 showmapping 检查一下

![[MIT 6.828] Lab 2 9](img\[MIT 6.828] Lab 2 9.png)

确实被更改了，没什么问题。

## Address Space Layout Alternatives

我们在 JOS 中使用的地址空间布局不是唯一可能的布局。操作系统可以将内核映射在低线性地址，同时将线性地址空间的上半部分留给用户进程。然而，x86 内核通常不采取这种方法，因为 x86 的向后兼容模式之一，即虚拟 8086 模式，在处理器中是“硬连线”使用线性地址空间的底部部分的，如果内核映射在那里，就根本无法使用该模式。

甚至有可能设计内核，使其不必为自己保留处理器线性或虚拟地址空间的任何固定部分，而是有效地允许用户级进程无限制地使用整个 4GB 虚拟地址空间——同时仍然完全保护内核免受这些进程的影响，并保护不同进程免受彼此的影响。

### Challenge 3

> 每个用户级环境都映射了内核。更改 JOS，使内核拥有自己的页表，并使一用户级环境在运行时只映射最少数量的内核页。也就是说，每个用户级环境只映射刚好足够让用户级环境正确进入和离开内核的页。你还必须想出一个计划，让内核读取/写入系统调用的参数。

如果要写的话感觉有点变态，所以说一下思路

首先，“用户级环境在运行时只映射最少数量的内核页”。这个过程中，CPU 先收到中断信号，读 IDT；然后发现需要切 r0，然后就去读任务状态段去找内核栈地址；CPU 将用户的寄存器压入内核栈；CPU 跳转到中断入口，开始执行内核指令。

所以，用户页表内至少得映射四个内核页面：存 IDT、GDT 的页（R）；存 TSS 的页（R）；存内核栈的页（RW）；存中断入口的页（RX）。其它的就不需要了

然后如何维护？维护两套页表，一套用户态，一套内核态。用户态的里面塞用户数据、代码，以及上面这四个页；内核态里面就是完整内核映射。

最后，“让内核读取/写入系统调用的参数”。按我们的设想，一进入中断，内核就切换到了内核页表，如果没映射用户的低虚拟地址，那么内核一读指针就 Page Fault 了。

怎么解决呢？我们可以设一个缓冲区，分配一个 1 页大小的共享内存，这个页同时存在于用户态和内核态中。当用户想传数据给内核态时，就把数据拷贝在这个共享缓冲区中。内核直接在缓冲区内读取数据即可。


### Challenge 4

> 写一个大纲，说明如何设计一个内核以允许用户环境无限制地使用完整的 4GB 虚拟和线性地址空间。提示：先做前一个挑战练习，将内核减少到用户环境中的几个映射。提示：该技术有时被称为“跟随跳动的内核 (follow the bouncing kernel)”。在你的设计中，一定要确切地说明当处理器在内核模式和用户模式之间转换时必须发生什么，以及内核将如何完成这种转换。还要描述在这种方案中内核将如何访问物理内存和 I/O 设备，以及内核在系统调用等期间将如何访问用户环境的虚拟地址空间。最后，思考并描述这种方案在灵活性、性能、内核复杂性和你能想到的其他因素方面的优缺点。

这个更变态。

在 x86 架构中，当发生系统调用时，CPU 必须立刻从 IDT 找到中断处理程序的线性地址并执行。也就是说，内核的中断入口必须时刻存在于当前的线性地址空间中。如果想让用户环境无限制地使用整个空间，那只能让内核的中断入口不固定位置，随时换位置。

那么，首先内核得随时监控用户进程的内存分配情况。当用户态有空闲的、未被映射的页，内核就把自己的入口代码映射到那里。此时内核还得更新寄存器（IDTR、GDTR、TR 等），告诉 CPU 内核状态更新了。并且，如果用户态此时 malloc 申请了内核目前所在的线性地址，内核得立刻寻找下一个空闲地址，把入口代码移动“跳动”过去。

如何进行状态转换呢？

用户态进入内核态时，发生系统调用，硬件根据寄存器地址直接跳入当前入口。此时依然用的是用户态的页表，只有入口页是内核态的。入口处的汇编将寄存器压入入口中的临时栈，`mov eax, PADDR(kern_pgdir); mov cr3, eax`，切换完成。

内核态进入用户态时，首先跳转到出口代码处，此时这个代码应该位于内核页表和用户页表中同一物理地址。然后就是`mov eax, PADDR(user_pgdir); mov cr3, eax`，`iret`，返回用户态。

物理内存和 I/O 设备的访问相对来说就简单一些。按照我们的设计，内核态会拥有整个 4GB 空间，内核可以直接从虚拟地址 0x00000000 开始，把所有的 RAM 直接一对一映射到自己的页表中，I/O 设备也是如此。

当内核接管 CPU 时，CR3 已经切换成了内核页表。此时如果用户态传入了一个指针，内核直接读这个虚拟地址，读到的是内核空间的数据，就 Page Fault 了。这就涉及到了系统调用的参数传递的问题。

内核虽然看不到用户的虚拟地址，但它映射了所有的物理内存。所以可以直接拿到用户的虚拟指针 VA，然后找到当前用户的页目录表物理基址。然后，用类似 Exercise 4 `pgdir_walk()` 的方式，模拟 CPU的查表过程，在用户的页目录和页表中，查出 VA 对应的真实物理地址 PA。最终，内核通过自己的物理内存映射区，直接读取物理地址 PA 里的数据。

最后是优缺点

优点很明显，它的空间利用率很好，用户态直接获得了 4GB 的空间，而且页表隔离的安全性也很好，毕竟用户页表里面除了那个入口代码以外根本没有内核的映射。

缺点也是显著的，它的性能开销太大了，每次系统调用、中断，都得切 CR3。而且明显能感觉到入口代码的维护难度特别大，不仅如此，估计这里读数据的开销也很大。

### Challenge 5

>  由于我们的 JOS 内核的内存管理系统仅以页粒度分配和释放内存，我们没有类似于通用的 `malloc/free` 设施可以在内核中使用。如果我们想要支持某些需要大于 4KB 的**物理连续**缓冲区的 I/O 设备，或者如果我们希望用户级环境（而不仅仅是内核）能够分配和映射 4MB **大页 (superpages)** 以获得最大的处理器效率，这可能是一个问题。（参见前面关于 PTE_PS 的挑战问题。）

这个问题现代 Linux 的“伙伴系统”（Buddy System）可以解决。大概就是，它会把内存按 2 的幂次方去划分，并两两配对。这里引入一个概念叫阶（Order）

* Order 0 = $2^0 \times 4\text{KB} = 4\text{KB}$ （单页）

* Order 1 = $2^1 \times 4\text{KB} = 8\text{KB}$ （两个连续页）

* ...

* Order 10 = $2^{10} \times 4\text{KB} = 4\text{MB}$ （1024 个连续页，刚好一个大页）

假设系统现在只有一块完整的 4MB（Order 10）空闲内存。用户申请一块 8KB（Order 1）的内存。此时内存分配器发现没有现成的 Order 1，于是把 4MB 的块一分为二，变成两个 2MB（Order 9）的“伙伴”，然后继续把其中一个 2MB 一分为二，变成两个 1MB（Order 8）……一路划分，直到分出两个 8KB（Order 1）的块。随后，把其中一个 8KB 给用户，另一个 8KB 挂到 Order 1 的空闲链表上。

当用户归还那块 8KB 内存时，内存分配器首先检查这个内存的”伙伴“是否空闲，如果空闲，就把它俩合并，拼成一个 16KB（Order 2）的块。然后递归，如果一路空闲，最终会拼回 4MB（Order 10），如果有块不空闲就停下。

这样的话，之前的`page_free_list`就得从链表改成数组，每个元素代表一个 Order 的空闲链表。`struct PageInfo`也得修改，让每个页能记住自己当前所属的 Order，以及它是否空闲。

如何寻找伙伴？如果当前块的首页索引是 page_idx，它的块大小是 1 << order（页数），那么那么它的伙伴的索引 `buddy_idx = page_idx ^ (1 << order);`，跟树有点像。

听起来好像很简单，但是我没在 JOS 里面写出来，以后有机会补一下，至少我感觉我的思路没啥问题。

# 总结

感觉一个 Lab 不能分成几天看，不然过两天就忘了前几天写了什么代码，写着写着就给自己都搞迷惑了

最后 make grade 一下，Lab 1 的时候还忘了这茬。如果`/usr/bin/env: ‘python’: No such file or directory`，直接把 `#!/usr/bin/env python`改成`#!/usr/bin/env python3`

![[MIT 6.828] Lab 2 10](img\[MIT 6.828] Lab 2 10.png)

