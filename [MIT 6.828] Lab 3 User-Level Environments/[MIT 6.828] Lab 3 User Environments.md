本实验主要完成两个部分：用户环境与异常处理；缺页异常、断点与系统调用。

# Part A: User Environments and Exception Handling

新包含的文件 `inc/env.h` 包含了 JOS 中用户环境的基本定义。内核使用 `Env` 数据结构来跟踪每个用户环境。在本实验中，你最初只会创建一个环境，但你需要设计 JOS 内核以支持多个环境；Lab 4 将利用这一功能，允许一个用户环境 fork 其他环境。

 `kern/env.c` 中，内核维护了三个与环境有关的主要全局变量：

```c
struct Env *envs = NULL;		// 所有环境
struct Env *curenv = NULL;		// 当前环境
static struct Env *env_free_list;	// 空闲环境列表
```

一旦 JOS 启动并运行起来，`envs` 指针将指向一个 `Env` 结构数组，代表系统中的所有环境。在我们的设计中，JOS 内核最多支持同时运行 `NENV` 个活动环境，尽管在任何给定时间通常运行的环境要少得多。(`NENV` 是 `inc/env.h` 中 `#define` 的一个常量。) 一旦分配，`envs` 数组将包含 `NENV` 个可能的环境各自对应的单个 `Env` 数据结构实例。

JOS 内核将所有非活动的 `Env` 结构保存在 `env_free_list` 上。这种设计使得环境的分配和释放变得容易，因为它们只需要在空闲列表中添加或删除即可。

内核使用 `curenv` 符号在任何给定时间跟踪当前正在执行的环境。在系统引导期间，运行第一个环境之前，`curenv` 最初被设置为 `NULL`。

## Environment State

`Env` 结构在 `inc/env.h` 中定义如下（未来实验中将添加更多字段）：

```c
struct Env {
	struct Trapframe env_tf;	// 保存的寄存器
	struct Env *env_link;		// 下一个空闲的 Env
	envid_t env_id;			// 唯一的环境标识符
	envid_t env_parent_id;		// 该环境父级的 env_id
	enum EnvType env_type;		// 指示特殊的系统环境
	unsigned env_status;		// 环境状态
	uint32_t env_runs;		// 环境运行的次数

	// 地址空间
	pde_t *env_pgdir;		// 页目录的内核虚拟地址
};
```

以下是各 `Env` 字段的用途：

- **`env_tf`**：此结构定义在 `inc/trap.h` 中，用于在环境未运行时保存环境的寄存器值：例如，当内核或另一个环境正在运行时。内核在从用户模式切换到内核模式时保存这些值，以便环境稍后可以在它中断的地方恢复。
- **`env_link`**：这是指向 `env_free_list` 上下一个 `Env` 的链接。`env_free_list` 指向列表上的第一个空闲环境。
- **`env_id`**：内核在此存储一个唯一标识符，代表当前使用该 `Env` 结构（即使用 `envs` 数组中该特定槽位）的环境。在用户环境终止后，内核可能会将相同的 `Env` 结构重新分配给不同的环境——但即使新环境重用了 `envs` 数组中的同一槽位，它也会拥有与旧环境不同的 `env_id`。
- **`env_parent_id`**：内核在此处存储创建此环境的那个环境的 `env_id`。通过这种方式，环境可以形成一个“族谱”，这将有助于为“谁允许对谁做什么”做出安全决策。
- **`env_type`**：用于区分特殊环境。对于大多数环境，它将是 `ENV_TYPE_USER`。在后续实验中，我们将为特殊系统服务环境引入几种更多类型。
- **`env_status`**：该变量保存以下值之一： **ENV_FREE**：指示 `Env` 结构非活动，因此在 `env_free_list` 上。 **ENV_RUNNABLE**：指示 `Env` 结构代表一个正在等待在处理器上运行的环境。 **ENV_RUNNING**：指示 `Env` 结构代表当前正在运行的环境。 **ENV_NOT_RUNNABLE**：指示 `Env` 结构代表一个当前处于活动状态但尚未准备好运行的环境：例如，因为它正在等待来自另一个环境的进程间通信 (IPC)。 **ENV_DYING**：指示 `Env` 结构代表一个僵尸环境。僵尸环境将在下一次陷入内核时被释放。直到 Lab 4 我们才会用到此标志。
- **`env_pgdir`**：此变量保存此环境页目录的内核**虚拟地址**。

就像 Unix 进程一样，JOS 环境将“线程 (thread)”和“地址空间 (address space)”的概念耦合在一起。线程主要由保存的寄存器（`env_tf` 字段）定义，地址空间由 `env_pgdir` 指向的页目录和页表定义。为了运行一个环境，内核必须同时使用保存的寄存器和适当的地址空间来设置 CPU。

我们的 `struct Env` 类似于 xv6 中的 `struct proc`。两个结构都将环境（即进程）的用户模式寄存器状态保存在 `Trapframe` 结构中。在 JOS 中，单个环境不像 xv6 中的进程那样有自己的内核栈。JOS 内核中同时只能有一个处于活动状态的环境，因此 JOS 只需要一个单一的内核栈。

## Allocating the Environments Array

在 Lab 2 中，你在 `mem_init()` 中为 `pages[]` 数组分配了内存，这是内核用来记录哪些页空闲、哪些页已用的表。你现在需要进一步修改 `mem_init()` 以分配一个类似的 `Env` 结构数组，称为 `envs`。

### Exercise 1

> 修改 `kern/pmap.c` 中的 `mem_init()`，以分配并映射 `envs` 数组。此数组由正好 `NENV` 个 `Env` 结构实例组成，分配方式非常类似于你分配 `pages` 数组的方式。同样像 `pages` 数组一样，支持 `envs` 的内存也应该在 `UENVS`（定义在 `inc/memlayout.h` 中）被映射为用户只读，以便用户进程可以读取此数组。
>
>  你应该运行你的代码并确保 `check_kern_pgdir()` 成功。

有一说一，直接把这个环境理解成进程更方便一点，它跟 unix 进程比起来就是没有独立的内核栈、同一时间只有一个活跃进程，其它差不多。

想要管理进程，首先得要一个数据结构来记录进程状态。这里其实就是在分配这个 envs 数组，跟 Lab 2 的 pages 数组差不多。在分配完物理内存后，修改页目录和页表，把这块物理内存映射到特定的虚拟地址 UENVS，并设置好访问权限。

首先 boot_alloc() 分配内存，分配 NENV 个 Env

```c
size_t envs_size = NENV * sizeof(struct Env);
envs = (struct Env *) boot_alloc(envs_size);
memset(envs, 0, envs_size);
```

然后就该建立页表映射，映射到 UENVS 且用户只读

```c
envs_size = ROUNDUP(NENV * sizeof(struct Env), PGSIZE);
boot_map_region(kern_pgdir, UENVS, envs_size, PADDR(envs), PTE_U | PTE_P);
```

其实没理解为什么要让用户只读，一般来说用户态想读进程状态的话，得有个系统调用，这里直接让用户像读变量一样去读取状态？可能是为了开销着想吧

make qemu 后发现

```shell
kernel panic at kern/pmap.c:153: PADDR called with invalid kva 00000000

153：    kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir) | PTE_U | PTE_P;
```

mem_init 在开头就炸了，但是我 Lab 2 的时候没报过错？

写了一点调试

```c
static void *
boot_alloc(uint32_t n)
{
	...
	if (!nextfree) {
		extern char end[];
		nextfree = ROUNDUP((char *) end, PGSIZE);
		cprintf("DEBUG boot_alloc: end=%08x, initial nextfree=%08x\n", (uint32_t)end, (uint32_t)nextfree);
	}
	...
}
```

```c
void
mem_init(void)
{
	uint32_t cr0;
	size_t n;

	// Find out how much memory the machine has (npages & npages_basemem).
	i386_detect_memory();

	// Remove this line when you're ready to test this function.
	// panic("mem_init: This function is not finished\n");

	//////////////////////////////////////////////////////////////////////
	// create initial page directory.
	kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
	cprintf("DEBUG mem_init: kern_pgdir allocated at %08x\n", (uint32_t)kern_pgdir);
	memset(kern_pgdir, 0, PGSIZE);
    ...
}
```

发现

```shell
DEBUG boot_alloc: end=f018f000, initial nextfree=f018f000
DEBUG mem_init: kern_pgdir allocated at f018f000               
kernel panic at kern/pmap.c:156: PADDR called with invalid kva 00000000 
```

神了，地址分配的一点问题没有，但是 PADDR called with invalid kva 00000000 了

review ，我的 boot_alloc 是从 end 作为起始地址的。检查 kernel.ld，给 .bss 加了 `*(COMMON)`

```c
	.bss : {
		PROVIDE(edata = .);
		*(.bss)
		*(COMMON)
		PROVIDE(end = .);
		BYTE(0)
	}
```

怀疑是我环境里的编译器会把未初始化的全局变量标记成 COMMON 块，但是这里的内存布局没有 COMMON，那么它们的首地址会放在 .bss 之后，也就是会在 end 后面，那 boot_alloc 然后 memset 的时候，就把这些变量清零。

```c
kern_pgdir = (pde_t *) boot_alloc(PGSIZE);
cprintf("DEBUG mem_init: kern_pgdir allocated at %08x\n", (uint32_t)kern_pgdir);
memset(kern_pgdir, 0, PGSIZE);
kern_pgdir[PDX(UVPT)] = PADDR(kern_pgdir) | PTE_U | PTE_P;
```

比如这里，就把 kern_pgdir 自己清空掉了。

加了 *(COMMON) 后编译通过了，那我 Lab 2 纯纯的运气好，才没被这个点卡住，也不知道为什么 JOS 没把这里写好。

## Creating and Running Environments

现在你将要在 `kern/env.c` 中编写运行用户环境所需的代码。由于我们还没有文件系统，我们将设置内核加载一个直接嵌入在内核本身中的静态二进制映像。JOS 将此二进制文件作为 ELF 可执行映像嵌入内核中。

Lab 3 的 `GNUmakefile` 在 `obj/user/` 目录中生成了许多二进制映像。如果你查看 `kern/Makefrag`，你会注意到一些“魔法”会将这些二进制文件直接“链接”到内核可执行文件中，就像它们是 `.o` 文件一样。链接器命令行上的 `-b binary` 选项会导致这些文件作为“原始”的未解释二进制文件链接进来，而不是由编译器生成的常规 `.o` 文件。（就链接器而言，这些文件根本不必是 ELF 映像——它们可以是任何东西，如文本文件或图片）如果在构建内核后查看 `obj/kern/kernel.sym`，你会注意到链接器“神奇地”产生了许多具有晦涩名称的符号，比如 `_binary_obj_user_hello_start`、`_binary_obj_user_hello_end` 和 `_binary_obj_user_hello_size`。链接器通过修饰二进制文件的文件名来生成这些符号名；这些符号为常规内核代码提供了引用嵌入式二进制文件的方法。

在 `kern/init.c` 中的 `i386_init()` 中，你将看到在环境中运行其中一个二进制映像的代码。然而，设置用户环境的关键函数尚未完成；你需要将它们填补完整。

### Exercise 2

> 在文件 `env.c` 中，完成以下函数的编码：
>
> - `env_init()`：初始化 `envs` 数组中的所有 `Env` 结构并将它们添加到 `env_free_list` 中。它还调用 `env_init_percpu`，后者会配置段硬件，为特权级 0（内核）和特权级 3（用户）配置分离的段。
> - `env_setup_vm()`：为新环境分配一个页目录，并初始化新环境地址空间的内核部分。
> - `region_alloc()`：为环境分配和映射物理内存。
> - `load_icode()`：你需要像引导加载程序所做的那样解析一个 ELF 二进制映像，并将其内容加载到新环境的用户地址空间中。
> - `env_create()`：使用 `env_alloc` 分配一个环境，并调用 `load_icode` 将 ELF 二进制文件加载到其中。
> - `env_run()`：在用户模式下启动运行指定的环境。
>
> 在你编写这些函数时，你可能会发现新的 cprintf 动词 `%e` 很有用——它会打印与错误代码相对应的描述。例如，
>
> ```C
> 	r = -E_NO_MEM;
> 	panic("env_alloc: %e", r);
> ```
>
> 将触发 panic 并显示信息 "env_alloc: out of memory"。

#### env_init

按注释的要求，为了让 Env 在 envs 中的顺序与 env_free_list 中一致，得把 envs 逆序插入。最后得调 env_init_precpu() 配置硬件分段机制。

```c
void
env_init(void)
{
	// Set up envs array
	// LAB 3: Your code here.

	int i;
	env_free_list = NULL;
	for (i = NENV - 1; i >= 0; i--) {
		envs[i].env_status = ENV_FREE;
		envs[i].env_id = 0;
		envs[i].env_link = env_free_list;
		env_free_list = &envs[i];
	}

	// Per-CPU part of the initialization
	env_init_percpu();
}
```

#### env_setup_vm

每当创建一个新线程时，内核必须分配一个新的页目录，其内核部分必须与内核初始的页目录一致，以保证进程在切换为内核态时能够正确访问内核。

首先用 page_alloc 分配页作为页目录，然后把 kern_pgdir 的内容复制过来，并记录其内核虚拟地址。

```c
static int
env_setup_vm(struct Env *e)
{
	int i;
	struct PageInfo *p = NULL;

	// Allocate a page for the page directory
	if (!(p = page_alloc(ALLOC_ZERO)))
		return -E_NO_MEM;

	// LAB 3: Your code here.

	p->pp_ref++; // 增加页目录所在物理页的引用计数
	e->env_pgdir = (pde_t *) page2kva(p);

	memcpy(e->env_pgdir, kern_pgdir, PGSIZE);
	// UVPT maps the env's own page table read-only.
	// Permissions: kernel R, user R
	e->env_pgdir[PDX(UVPT)] = PADDR(e->env_pgdir) | PTE_P | PTE_U;

	return 0;
}
```

#### region_alloc

当操作系统加载一个可执行文件时，需要为它在用户空间分配物理内存，并映射到特定的虚拟地址。region_alloc 就是用来连续分配一段内存区域的。

这里直接循环分配物理页，并把它插入到当前进程的页目录就行。

```c
static void
region_alloc(struct Env *e, void *va, size_t len)
{
    // LAB 3: Your code here.
    // (But only if you need it for load_icode.)
    
    // 将起始和结束地址页对齐
    void *st = (void *) ROUNDDOWN((uint32_t)va, PGSIZE);
    void *ed = (void *) ROUNDUP((uint32_t)va + len, PGSIZE);
    void *i;
    struct PageInfo *p = NULL;

    for (i = st; i < ed; i += PGSIZE) {
        p = page_alloc(0);
        if (p == NULL) {
            panic("region_alloc: out of memory\n");
        }
        
        // 将分配的物理页映射到环境 e 的页表中
        if (page_insert(e->env_pgdir, p, i, PTE_W | PTE_U) < 0) {
            panic("region_alloc: page_insert failed\n");
        }
    }
}
```

#### load_icode

从注释中可以发现，这个函数是去读取并解析 ELF文件，然后把它加载到虚拟内存中。

那么，首先验证 magic number，确保它是 elf。然后解析出加载地址，这里的加载地址应该是用户虚拟地址，所以得先切 cr3，指向 e -> env_pgdir，再去加载数据。

对于每一个要加载的段，用 region_alloc 去分配内存，对于 bss 段，文件中不储存数据，只记录长度，所以剩下的 p_memsz - p_filesz 清零。

然后就是设置程序入口，再次切 cr3，恢复内核地址空间，最后分配用户栈。

```c
static void
load_icode(struct Env *e, uint8_t *binary)
{
	// LAB 3: Your code here.
	struct Elf *elfhdr = (struct Elf *) binary;
    struct Proghdr *ph, *eph;
	if (elfhdr->e_magic != ELF_MAGIC) {
        panic("load_icode: invalid ELF magic number\n");
    }

	lcr3(PADDR(e->env_pgdir));

	//遍历 Program Headers，加载所有段
	ph = (struct Proghdr *) ((uint8_t *) elfhdr + elfhdr->e_phoff);
	eph = ph + elfhdr->e_phnum;
	for (; ph < eph; ph++) {
        if (ph->p_type == ELF_PROG_LOAD) {
            region_alloc(e, (void *) ph->p_va, ph->p_memsz);
            // 将数据复制到对应的虚拟地址
            memcpy((void *) ph->p_va, binary + ph->p_offset, ph->p_filesz);
            // BSS 段清零
            memset((void *) (ph->p_va + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
        }
    }
	
	// 将入口指针保存到 Trapframe 的 EIP 寄存器中
	e->env_tf.tf_eip = elfhdr->e_entry;
	lcr3(PADDR(kern_pgdir));


	// Now map one page for the program's initial stack
	// at virtual address USTACKTOP - PGSIZE.

	// LAB 3: Your code here.
	region_alloc(e, (void *) (USTACKTOP - PGSIZE), PGSIZE);
}
```

#### env_create

这个很直接，就是分配全新的环境，把二进制文件装载进去，然后设置其环境类型。

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
	// LAB 3: Your code here.
	struct Env *e;
    int r;

	r = env_alloc(&e, 0);
    if (r < 0) {
        panic("env_create: %e", r);
    }

    load_icode(e, binary);
    e->env_type = type;
}
```

#### env_run

这里做上下文切换，把 CPU 的控制权移交给用户程序。理一下逻辑。

如果 CPU 正在运行某个环境（curenv 不为空且状态为 ENV_RUNNING），需将其状态切换为 ENV_RUNNABLE。

将 curenv 指向即将在 CPU 上运行的新环境 e，并将其状态置为ENV_RUNNING，同时增加其运行计数。

将 cr3 指向新环境的页目录物理地址。用 env_pop_tf 从内核态切到用户态。

```c
void
env_run(struct Env *e)
{
	// LAB 3: Your code here.

	if (curenv != NULL && curenv->env_status == ENV_RUNNING) {
        curenv->env_status = ENV_RUNNABLE;
    }

	curenv = e;
    curenv->env_status = ENV_RUNNING;
    curenv->env_runs++;

	lcr3(PADDR(curenv->env_pgdir));
	env_pop_tf(&curenv->env_tf);

	// panic("env_run not yet implemented");
}
```

这里瞄一眼 env_pop_tf

```c
//
// Restores the register values in the Trapframe with the 'iret' instruction.
// This exits the kernel and starts executing some environment's code.
//
// This function does not return.
//
void
env_pop_tf(struct Trapframe *tf)
{
	asm volatile(
		"\tmovl %0,%%esp\n"
		"\tpopal\n"
		"\tpopl %%es\n"
		"\tpopl %%ds\n"
		"\taddl $0x8,%%esp\n" /* skip tf_trapno and tf_errcode */
		"\tiret\n"
		: : "g" (tf) : "memory");
	panic("iret failed");  /* mostly to placate the compiler */
}
```

可以看见，它会用一串 pop 去恢复寄存器，然后 iret。iret 会恢复 cs, eip, esp 等寄存器，从内核态切换为用户态，并跳到用户态程序的入口点。

最后编译

![[MIT 6.828 Lab 3] 1](img\[MIT 6.828 Lab 3] 1.png)

发现 Triple fault 了。下意识地以为自己写错了，然后发现确实该 Triple fault。

---

以下是在调用用户代码之前的代码调用图。确保你理解每一步的目的。

- `start` (`kern/entry.S`)
- `i386_init` (`kern/init.c`)
  - `cons_init`
  - `mem_init`
  - `env_init`
  - `trap_init` （此时仍未完成）
  - `env_create`
  - `env_run`
    - `env_pop_tf`

完成之后，你应该编译内核并在 QEMU 下运行它。如果一切顺利，你的系统应进入用户空间并执行 `hello` 二进制文件，直到它使用 `int` 指令发出系统调用。在此时该系统调用不能成功运行，因为 JOS 没有设置硬件以允许任何从用户空间进入内核的转换。当 CPU 发现它没有被设置为处理该系统调用中断时，它将产生一般保护异常 ，当发现无法处理它时，会产生双重错误异常，又发现依然无法处理后，最后它会放弃并产生所谓的“三重错误”。通常，在此之后你会看到 CPU 重置并且系统重启。虽然这对于传统应用程序很重要，但对内核开发而言却是个痛点，因此在打过补丁的 6.828 QEMU 中，你将看到寄存器转储以及一条 "Triple fault." 的消息。

我们将很快解决这个问题，但现在我们可以使用调试器来检查我们是否进入了用户模式。使用 `make qemu-gdb` 并在 `env_pop_tf` 设置 GDB 断点，这应该是你在实际进入用户模式前碰到的最后一个函数。使用 `si` 单步执行此函数；处理器应在 `iret` 指令后进入用户模式。随后你应该看到用户环境可执行文件中的第一条指令，即 `lib/entry.S` 标号 `start` 处的 `cmpl` 指令。现在使用 `b *0x...` 在 `hello` 中的 `sys_cputs()` 处的 `int $0x30` 设置一个断点（请参阅 `obj/user/hello.asm` 以获取用户空间地址）。此 `int` 是向控制台显示字符的系统调用。如果你无法执行到 `int`，则说明你的地址空间设置或程序加载代码存在问题；请回去修复它，然后再继续。

## Handling Interrupts and Exceptions

在此时，用户空间中的第一个 `int $0x30` 系统调用指令是一条死路：一旦处理器进入用户模式，就没有返回的途径。你现在需要实现基本的中断和系统调用处理机制，以便内核能够从用户模式代码中恢复对处理器的控制。你要做的第一件事是彻底熟悉 x86 的中断和异常机制。

### Exercise 3

> 如果你还没读过，请阅读 80386 程序员手册的第 9 章，异常和中断（或者 IA-32 开发者手册的第 5 章）。

pass

---

在本实验中，我们通常遵循 Intel 的术语：异常 (exceptions)、中断 (interrupts) 等。然而，“异常”、“陷入”、“中断”、“错误”和“中止”等术语在不同的架构和操作系统之间没有标准含义，常常被忽略掉它们在特定架构（如 x86）上的细微差别而被混用。当你在本实验外看到这些术语时，含义可能会略有不同。

## Basics of Protected Control Transfer

异常和中断都是“受保护控制转移”，它们会使处理器从用户模式切换到内核模式 (CPL=0)，同时不会让用户模式代码有机会干扰内核或其他环境的运作。在 Intel 的术语中，**中断**是一种受保护控制转移，通常由处理器外部的异步事件引起，例如外部设备 I/O 活动通知。相反，**异常**是由于当前运行的代码引发的同步受保护控制转移，例如由于除以零或无效内存访问引起的。

为了确保这些受保护控制转移实际上是**受保护**的，处理器的中断/异常机制被设计为，发生中断或异常时当前正在运行的代码**无法任意选择**进入内核的位置或方式。相反，处理器确保内核只能在受严格控制的条件下进入。在 x86 上，有两种机制共同提供这种保护：

- **中断描述符表 (IDT)**：处理器确保中断和异常只能使内核在少数特定的、由**内核自己决定**的明确入口点进入，而不是由中断或异常发生时运行的代码决定的。 x86 允许多达 256 个不同的中断或异常进入内核的入口点，每个对应一个不同的**中断向量 (interrupt vector)**。向量是一个介于 0 和 255 之间的数字。中断的向量由中断源决定：不同的设备、错误条件以及对内核的应用请求会生成具有不同向量的中断。CPU 使用该向量作为处理器**中断描述符表** (IDT) 中的索引，由内核将其设置在内核私有内存中，很像 GDT。处理器从该表中适当的条目加载：
  - 要加载到指令指针寄存器 (EIP) 的值，指向被指定处理此类异常的内核代码。
  - 要加载到代码段寄存器 (CS) 的值，该值在第 0-1 位包含了异常处理程序将要运行的特权级。（在 JOS 中，所有异常都在内核模式特权级 0 处理）。
- **任务状态段 (TSS)**：处理器需要一个地方在中断或异常发生前保存**旧**处理器状态（例如，处理器调用异常处理程序前的原始 `EIP` 和 `CS` 值），以便异常处理程序随后可以恢复该旧状态并从中断的代码继续执行。但是，这个用来保存旧处理器状态的区域本身必须被保护，以免受无特权用户代码的影响；否则存在缺陷或恶意的用户代码会危及内核。 由于这个原因，当 x86 处理器接受导致将特权级从用户更改为内核模式的中断或陷阱时，它还会切换到位于内核内存中的堆栈。一个称为**任务状态段** (TSS) 的结构指定了这个堆栈所在的段选择器和地址。处理器将 `SS`、`ESP`、`EFLAGS`、`CS`、`EIP` 及可选的错误代码压入（该新堆栈）中。然后，它从中断描述符加载 `CS` 和 `EIP`，并设置 `ESP` 和 `SS` 以引用该新堆栈。 尽管 TSS 很大且可用于各种用途，JOS 仅使用它来定义处理器在从用户模式切换到内核模式时应该切换到的内核堆栈。因为 JOS 中的“内核模式”对应于 x86 的特权级 0，处理器进入内核模式时使用 TSS 的 `ESP0` 和 `SS0` 字段定义内核堆栈。JOS 不使用任何其他 TSS 字段。

## Types of Exceptions and Interrupts

x86 处理器内部可以生成的所有同步异常，都使用介于 0 和 31 之间的中断向量，并因此映射到 IDT 条目 0-31。例如，页错误始终通过向量 14 引发异常。大于 31 的中断向量仅用于**软件中断**（由 `int` 指令生成）或**硬件中断**（由外部设备引起，当它们需要关注时发出的异步中断）。

在本节中，我们将扩展 JOS 以处理向量 0-31 中内生的 x86 异常。在下一节中，我们将使 JOS 处理软件中断向量 48 (0x30)，JOS（相当任意地）将其用作系统调用中断向量。在 Lab 4 中，我们将扩展 JOS 以处理如时钟中断等由外部生成的硬件中断。

## An Example

让我们把这些部分放在一起并追踪一个例子。假设处理器正在执行用户环境中的代码，并遇到试图除以零的除法指令。

1. 处理器切换到 TSS 中的 `SS0` 和 `ESP0` 字段定义的堆栈，这些字段在 JOS 中将分别保存值 `GD_KD` 和 `KSTACKTOP`。
2. 处理器将异常参数推入内核堆栈中，从地址 `KSTACKTOP` 开始：

```c
                     +--------------------+ KSTACKTOP             
                     | 0x00000 | old SS   |     " - 4
                     |      old ESP       |     " - 8
                     |     old EFLAGS     |     " - 12
                     | 0x00000 | old CS   |     " - 16
                     |      old EIP       |     " - 20 <---- ESP 
                     +--------------------+
```

3. 由于我们正在处理的是除法错误（x86 上的中断向量 0），处理器读取 IDT 条目 0，并设置 `CS:EIP` 以指向条目描述的处理函数。

4. 处理函数接管并处理该异常，例如通过终止该用户环境。

对于某些类型的 x86 异常，除上述“标准”五个字外，处理器还会压入另一个包含**错误代码**的字到堆栈中。页错误异常（编号 14）是一个重要的例子。查阅 80386 手册以确定处理器在哪些异常编号下压入错误代码，以及这些情况下的错误代码意味着什么。当处理器压入错误代码时，从用户模式进入异常处理程序之初，堆栈将如下所示：

```c
                     +--------------------+ KSTACKTOP             
                     | 0x00000 | old SS   |     " - 4
                     |      old ESP       |     " - 8
                     |     old EFLAGS     |     " - 12
                     | 0x00000 | old CS   |     " - 16
                     |      old EIP       |     " - 20
                     |     error code     |     " - 24 <---- ESP
                     +--------------------+
```

## Nested Exceptions and Interrupts

处理器可以在内核和用户模式下同时接受异常和中断。然而，只有在从用户模式进入内核时，x86 处理器才会自动切换堆栈，然后再将其旧寄存器状态压入堆栈并通过 IDT 调用适当的异常处理程序。如果在发生中断或异常时，处理器**已经**处于内核模式（即 `CS` 寄存器的低 2 位已为 0），则 CPU 只会在**同一**内核堆栈中压入更多值。这样，内核就能够优雅地处理内核本身代码中引起的**嵌套异常**。这种功能在实现保护时也是一个重要的工具，正如稍后在系统调用部分我们将看到的。

如果处理器处于内核模式并接受一个嵌套异常，由于它不需要切换栈，因此它不会保存旧的 `SS` 或 `ESP` 寄存器。对于不压入错误代码的异常类型，当进入异常处理程序时，内核堆栈如下所示：

```c
                     +--------------------+ <---- old ESP
                     |     old EFLAGS     |     " - 4
                     | 0x00000 | old CS   |     " - 8
                     |      old EIP       |     " - 12
                     +--------------------+
```

对于压入错误代码的异常类型，处理器会像以前那样在旧的 `EIP` 之后紧跟着压入错误代码。

关于处理器的嵌套异常功能有一个重要提醒。如果处理器在已处于内核模式时接受了一个异常，但出于任何原因（如栈空间不足）**无法将其旧状态压入内核栈中**，那么处理器无法恢复，因此它只能自动重置（重启）。毋庸置疑，内核应该被设计成不会发生这种事。

## Setting Up the IDT

现在你已经拥有了设置 IDT 并处理 JOS 异常所需的基本信息。目前，你将设置 IDT 来处理中断向量 0-31（即处理器异常）。我们将在本实验后续处理系统调用中断，并在以后的实验中添加中断 32-47（设备 IRQ）。

头文件 `inc/trap.h` 和 `kern/trap.h` 包含了与你将要熟悉的中断和异常相关的重要定义。文件 `kern/trap.h` 包含了严格专属于内核私有的定义，而 `inc/trap.h` 包含了对用户层程序和库也可能大有帮助的定义。

**注意：** 0-31 范围内的有些异常被 Intel 定义为保留。因为这些保留的异常永远不会由处理器生成，因此其实你如何处理它们并不重要。以你认为最整洁的方式即可。

你应当实现的整体控制流如下所示：

```c
      IDT                   trapentry.S         trap.c
   
+----------------+                        
|   &handler1    |---------> handler1:          trap (struct Trapframe *tf)
|                |             // do stuff      {
|                |             call trap          // handle the exception/interrupt
|                |             // ...           }
+----------------+
|   &handler2    |--------> handler2:
|                |            // do stuff
|                |            call trap
|                |            // ...
+----------------+
       .
       .
       .
+----------------+
|   &handlerX    |--------> handlerX:
|                |             // do stuff
|                |             call trap
|                |             // ...
+----------------+
```

每个异常或中断都应该在 `trapentry.S` 中拥有独立的处理程序，且 `trap_init()` 应当使用这些处理程序的地址初始化 IDT。每个处理程序应该在栈上构建一个 `struct Trapframe`（见 `inc/trap.h`），然后使用指向 Trapframe 的指针作为参数调用 `trap()`（在 `trap.c` 中）。最后，`trap()` 处理异常/中断或分配给特定的处理程序函数。

### Exercise 4

> 编辑 `trapentry.S` 和 `trap.c`，并实现上述功能。在 `trapentry.S` 中的宏 `TRAPHANDLER` 和 `TRAPHANDLER_NOEC`，以及 `inc/trap.h` 中定义的 T_* 将会有所帮助。你需要针对 `inc/trap.h` 中定义的每一个 trap 在 `trapentry.S` 中添加一个入口点（使用前述宏），并且你还必须提供供 `TRAPHANDLER` 宏引用的 `_alltraps` 代码。你还需要修改 `trap_init()` 以初始化 `idt` 指向这些在 `trapentry.S` 定义的入口点；这里 `SETGATE` 宏会很有帮助。 你的 `_alltraps` 应当做到：
>
> 1. 压入所需的值，让堆栈看上去像个 `struct Trapframe`
> 2. 加载 `GD_KD` 到 `%ds` 和 `%es`
> 3. 执行 `pushl %esp` 来传递指向 Trapframe 的指针以作为给 `trap()` 的参数
> 4. 调用 `call trap`（`trap` 是否还有可能返回？）
>
> 考虑一下使用 `pushal` 指令，它与 `struct Trapframe` 的布局非常贴合。 使用在出现任何系统调用之前即发生异常的测试程序进行测试（在 `user` 目录下），比如 `user/divzero`。你现在应该能使 `make grade` 在 `divzero`、`softint` 以及 `badsegment` 测试用例上成功了。

总结一下，这里是要写一份 IDT，并且在汇编里编写代码去捕获这些异常，并将控制权交给内核的 C 处理函数。

首先还是整理异常处理时发生了什么

1. 从任务状态段 TSS 中读取内核栈的指针
2. 切内核栈
3. 将原来的 SS、CS、EIP 等压入内核栈
4. 某些异常还要多压入一个错误码
5. 根据 IDT 中的段选择子和偏移量，跳转到相应的异常处理入口

第四步有点特殊，为了让 C 的处理函数能够使用统一的结构体  Trapframe，得在汇编层对没有错误码的异常进行填充，压入一个 0。TRAPHANDLER_NOEC 和 TRAPHANDLER 就是干这个的。

#### trapentry.S

首先跟着  inc/trap.h 中的 Trap numbers 来写入口点，写 0-31 就行

```assembly
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
```

然后是 _alltraps，所有的宏最终都会无条件跳转到 _alltraps。此时，栈上已经包含了错误码、中断号以及原始的硬件上下文。根据 Exercise 要求写就行。

```assembly
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
```

#### trap.c

CPU 可不知道我们在汇编里定义的那一堆入口点标签，所以我们得将这些标签的内存地址注册到内核的 IDT 数组中

这里可以用 inc/mmu.h 的 `SETGATE(gate, istrap, sel, off, dpl)`，对于异常，一般将其设置为中断门（istrap = 0，执行时禁用其他中断），并且不允许用户态直接触发（DPL = 0）。

```c
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.

	void divide_entry();
    void debug_entry();
    void nmi_entry();
    void brkpt_entry();
    void oflow_entry();
    void bound_entry();
    void illop_entry();
    void device_entry();
    void dblflt_entry();
    void tss_entry();
    void segnp_entry();
    void stack_entry();
    void gpflt_entry();
    void pgflt_entry();
    void fperr_entry();
    void align_entry();
    void mchk_entry();
    void simderr_entry();

	SETGATE(idt[T_DIVIDE], 0, GD_KT, divide_entry, 0);
    SETGATE(idt[T_DEBUG],  0, GD_KT, debug_entry,  0);
    SETGATE(idt[T_NMI],    0, GD_KT, nmi_entry,    0);
    SETGATE(idt[T_BRKPT],  0, GD_KT, brkpt_entry,  0); 
    SETGATE(idt[T_OFLOW],  0, GD_KT, oflow_entry,  0);
    SETGATE(idt[T_BOUND],  0, GD_KT, bound_entry,  0);
    SETGATE(idt[T_ILLOP],  0, GD_KT, illop_entry,  0);
    SETGATE(idt[T_DEVICE], 0, GD_KT, device_entry, 0);
    SETGATE(idt[T_DBLFLT], 0, GD_KT, dblflt_entry, 0);
    SETGATE(idt[T_TSS],    0, GD_KT, tss_entry,    0);
    SETGATE(idt[T_SEGNP],  0, GD_KT, segnp_entry,  0);
    SETGATE(idt[T_STACK],  0, GD_KT, stack_entry,  0);
    SETGATE(idt[T_GPFLT],  0, GD_KT, gpflt_entry,  0);
    SETGATE(idt[T_PGFLT],  0, GD_KT, pgflt_entry,  0);
    SETGATE(idt[T_FPERR],  0, GD_KT, fperr_entry,  0);
    SETGATE(idt[T_ALIGN],  0, GD_KT, align_entry,  0);
    SETGATE(idt[T_MCHK],   0, GD_KT, mchk_entry,   0);
    SETGATE(idt[T_SIMDERR],0, GD_KT, simderr_entry,0);
	// Per-CPU setup 
	trap_init_percpu();
}
```

一样的，改一下 grade-lab3 `#!/usr/bin/env python3`，make grade。Part A 成功通过。

![[MIT 6.828 Lab 3] 2](img\[MIT 6.828 Lab 3] 2.png)

### Challenge 1

>  你现在在 `trapentry.S` 中的 `TRAPHANDLER` 列表以及在 `trap.c` 中的安装代码之间可能有很多非常相似的代码。清理一下。更改 `trapentry.S` 中的宏，以自动生成一个供 `trap.c` 使用的表。请注意，可以通过使用 `.text` 和 `.data` 指令，在汇编器里的代码生成和数据生成间进行切换。

修改 trapentry.S 中的宏定义，使其不仅在 .text 段生成代码，同时在 .data 段记录当前标签的地址

这里有一个坏处就是，在 .S 中得把所有的入口点都写进去，不能跳。对于保留的异常号也得写占位 Entry，确保 trap_entries 数组的索引与异常号严格一一映射。

```shell
...
###################################################################
# Data section for the array of trap entry points
###################################################################
.data
.p2align 2
.globl trap_entries
trap_entries:
...
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
...
```

```c
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.

	extern uint32_t trap_entries[];
	for (int i = 0; i <= 31; i++) {
		int dpl = 0;
		SETGATE(idt[i], 0, GD_KT, trap_entries[i], dpl);
	}
	// Per-CPU setup 
	trap_init_percpu();
}
```

此时再 make grade，发现还是 30/30 分，没问题。

但是有一说一，如果要这样写代码，那后面加进来的异常号，比如 SYSCALL 48，就有点不知道咋处理了，把中间继续填充占位符？那有点太多了

### Questions 1

> 请在你的 `answers-lab3.txt` 中回答以下问题：
>
> 1. 为每个异常/中断都有一个单独处理程序的目的何在？（换言之，如果所有异常/中断都被分发到同一个处理程序上，当前实现所具备的什么功能将无法提供？）
> 2. 你是否需要做任何操作来使得 `user/softint` 程序的行为正确？评分脚本期待其产生一个一般保护错误（trap 13），但 `softint` 的代码是写着 `int $14`。**为什么**这应当产生中断向量 13 呢？如果内核实际上允许 `softint` 的 `int $14` 指令来调用内核的页错误处理程序（它是中断向量 14），会发生什么？

1. 如果只用一个通用处理程序，那么当执行到 C 的 trap.c -> trap(struct Trapframe *tf) 时，tf -> tf_trapno 就是undefiend 的，也就是说，内核不能区分错误类型，之后的 dispatcher 也会无法进行。

   也就是说，我们定义这么多单独的处理程序，目的就在于进入通用逻辑之前，把错误类型补齐，即`pushl $(num)`

2. 执行 int $14 时，CPU 会发现这是非法调用，CPL (3) -> DPL(0)，特权级检查失败，CPU 拒绝此次中断，并出发 T_GPFLT 13。所以，测试脚本期望的 Trap 13 是硬件保护的产物，无需编写额外的内核代码来做保护。

   如果允许，也就是 DPL(3)的话，那用户态就可以通过 $int 14 进入内核态的 T_PGFLT 14，这意味着用户可以随意操控栈上的 CR2 寄存器的值（缺页地址）或错误码等。同时，这个能力也就意味着恶意程序能随意利用内核为非法地址分配物理页，从而绕过内存隔离，获取内核空间的任意读写权限，做到事实上的提权。

# Part B: Page Faults, Breakpoints Exceptions, and System Calls

既然你的内核具有了基本的异常处理能力，现在你将对其进行完善以提供依赖于异常处理的重要的操作系统原语。

## Handling Page Faults

页错误异常，即中断向量 14 (`T_PGFLT`)，是一个特别重要的异常，我们将在本次实验和接下来的实验中大量练习它。当处理器遇到页错误时，它将引起错误的线性（即虚拟）地址储存在特别的处理器控制寄存器 `CR2` 中。在 `trap.c` 中，我们提供了一个处理页错误异常的特殊函数 `page_fault_handler()` 的开头部分。

### Exercise 5

> 修改 `trap_dispatch()` 以将页错误异常分派给 `page_fault_handler()`。现在你应能够让 `make grade` 在 `faultread`、`faultreadkernel`、`faultwrite` 以及 `faultwritekernel` 测试用例上成功。如果其中哪项不工作，弄清原因并修复。切记你可以使用 `make run-x` 或者是 `make run-x-nox` 将 JOS 引导进入某个特定用户程序。举例来说，`make run-hello-nox` 将运行 `hello` 用户程序。

这里就是做异常处理的 dispatcher 了。

```c
static void
trap_dispatch(struct Trapframe *tf)
{
	// Handle processor exceptions.
	// LAB 3: Your code here.
	if (tf->tf_trapno == T_PGFLT) {
		page_fault_handler(tf);
		return;
	}
	// Unexpected trap: The user process or the kernel has a bug.
	print_trapframe(tf);
	if (tf->tf_cs == GD_KT)
		panic("unhandled trap in kernel");
	else {
		env_destroy(curenv);
		return;
	}
}
```

很简单的一段话，如果错误号是 T_PGFLT，就走页错误的 handler。这个 handler 是 Lab 3 源码自带的， 但是现在还不全面。之后再来分析这个异常处理具体的意义。

![[MIT 6.828 Lab 3] 3](img\[MIT 6.828 Lab 3] 3.png)



Exercise 提到的几个 fault 已经通过了。

---

在后面实现系统调用时，你将进一步完善内核对页错误的处理。

## The Breakpoint Exception

断点异常（中断向量 3，`T_BRKPT`）通常被调试器使用，通过用特殊的单字节软件中断 `int3` 指令临时替换相关程序指令，从而实现在程序代码中插入断点。在 JOS 中，我们将略微“滥用”这个异常：将其变成一个基础的伪系统调用，允许任何用户环境来唤醒 JOS 内核监视器。如果我们把 JOS 内核监视器当作原始调试器来看待的话，这种用法确实相当合理。例如在 `lib/panic.c` 中，用户模式实现的 `panic()` 即在显示它的 panic 消息后执行了一个 `int3`。

### Exercise 6

> 修改 `trap_dispatch()` 以使断点异常去调用内核监视器。现在你应该能够使得 `make grade` 对 `breakpoint` 的测试取得成功。

跟之前差不多，这次实现的是对 T_BRKPT 的处理。

```c
// 断点异常 T_BRKPT: 3
if (tf->tf_trapno == T_BRKPT) {
    monitor(tf);
    return;
}
```

这里我们把 Trapframe 直接传给 monitor，看 monitor 的源码可以知道它依赖该陷阱帧去还原断点处的寄存器状态，其实调的就是 trap.c 的 print_trapframe

有一个点要注意，make grade 后报错如下的话

![[MIT 6.828 Lab 3] 4](img\[MIT 6.828 Lab 3] 4.png)

你可以发现，TRAP frame at 0xf01d2000 下面，trap 的是 General Protection。这是因为我们之前写 IDT 表的时候，所有的权限都放的 0，这里我们得回去把 T_BRKPT 的权限改成 3.

```c
void
trap_init(void)
{
	extern struct Segdesc gdt[];

	// LAB 3: Your code here.

	extern uint32_t trap_entries[];
	for (int i = 0; i <= 31; i++) {
		// int dpl = 0;
		int dpl = (i == T_BRKPT) ? 3 : 0; // Exercise 6 fix
		SETGATE(idt[i], 0, GD_KT, trap_entries[i], dpl);
	}
	// Per-CPU setup 
	trap_init_percpu();
}
```

这样就可以了。

### Challenge 2

> 修改 JOS 内核监视器，使其能从当前位置 'continue' 执行（例如在使用断点异常调用内核监视器的情况下，从 `int3` 后继续），并且还能每次执行单条指令的 single-step。你将需要理解 `EFLAGS` 寄存器的某些位以实现此单步执行。
>
> **Optional:** 如果你觉得很有探险精神的话，去找点 x86 反汇编器源码——例如从 QEMU 里摘取，或从 GNU binutils 获取，或者是干脆你自己写一个——并将它扩展进 JOS 内核监视器，使得你在单步执行指令时也能对指令进行反汇编和显示。再与 Lab 1 中的符号表加载相结合起来，这就是制造真正内核调试器的核心配方了。

现在我们有 int 3 了，这里就是要加 c 和 si。我们用到 EFLAGS 寄存器的 TF 位，TF = 0 时，CPU 正常连续执行指令，TF = 1 时，CPU 进入单步模式，每执行一条指令就会触发一个 T_DEBUG。

我们在 monitor.c 中修改

```c
int mon_c(int argc, char **argv, struct Trapframe *tf) {
	if (tf == NULL) {
		cprintf("Error: No trapped environment to continue.\n");
		return 0;
	}
	// 清除 EFLAGS 中的 TF 位，对应掩码 FL_TF: 0x0100，保证后续指令连续执行
	tf->tf_eflags &= ~FL_TF;
	
	// 返回 -1，跳出 monitor 的 while (1)
	return -1;
}

int mon_si(int argc, char **argv, struct Trapframe *tf) {
	if (tf == NULL) {
		cprintf("Error: No trapped environment to single step.\n");
		return 0;
	}
	// 将 EFLAGS 中的 TF 位置为 1，让 CPU 自动触发 T_DEBUG 异常
	tf->tf_eflags |= FL_TF;
	
	return -1;
}
```

这里 return -1，会在 run_cmd() 中 `return commands[i].func(argc, argv, tf);`，然后在 monitor() 中 break。这里最开始的调用链是 _alltraps  -> trap() -> trap_dispatch() -> monitor()，返回到 trap() 时，`env_run(curenv);`，CPU 会切换为 curenv 的状态，最终 env_pop_tf，iret，回到发生中断的那条指令的下一条继续执行。

记得在 monitor.c 的 commands[] 中注册新写的这两个函数，monitor.h 声明这两个函数，也要在 trap_dispatch() 中拦截 T_DEBUG

```c
if (tf->tf_trapno == T_DEBUG) {
    monitor(tf);
    return;
}
```

make run-breakpoint

```shell

iplayforsg@ubuntu:~/Desktop/MIT_6.828$ make run-breakpoint
make[1]: Entering directory '/home/iplayforsg/Desktop/MIT_6.828'
+ cc kern/init.c
+ ld obj/kern/kernel
ld: warning: section `.bss' type changed to PROGBITS
+ mk obj/kern/kernel.img
make[1]: Leaving directory '/home/iplayforsg/Desktop/MIT_6.828'
qemu-system-i386 -drive file=obj/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::26000 -D qemu.log 
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
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01d2000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00802000
  edx  0x00000000
  ecx  0x0080202c
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x00000003 Breakpoint
  err  0x00000000
  eip  0x00800037
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfd0
  ss   0x----0023
K> si
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01d2000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdff0
  oesp 0xefffffdc
  ebx  0x00802000
  edx  0x00000000
  ecx  0x0080202c
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x00000001 Debug
  err  0x00000000
  eip  0x00800038
  cs   0x----001b
  flag 0x00000182
  esp  0xeebfdfd4
  ss   0x----0023
K> si
Incoming TRAP frame at 0xefffffbc
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
TRAP frame at 0xf01d2000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdff0
  oesp 0xefffffdc
  ebx  0x00802000
  edx  0x00000000
  ecx  0x0080202c
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x00000001 Debug
  err  0x00000000
  eip  0x00800073
  cs   0x----001b
  flag 0x00000182
  esp  0xeebfdfd8
  ss   0x----0023
K> c
Incoming TRAP frame at 0xefffffbc
TRAP frame at 0xf01d2000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfb0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0x00000003
  es   0x----0023
  ds   0x----0023
  trap 0x0000000d General Protection
  err  0x00000182
  eip  0x0080010b
  cs   0x----001b
  flag 0x00000006
  esp  0xeebfdf88
  ss   0x----0023
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
Welcome to the JOS kernel monitor!
Type 'help' for a list of commands.
K> 
```

可以看见，第一次是 Breakpoint 异常，后面几次 si 是 Debug 异常，最后 c 的时候是 General Protection。前面 si 的时候 eip 是一步一步走的，flag 寄存器的 TF 位也正确置 1 了。

后面 c 的时候异常我以为自己写错了，然后发现，`err  0x00000182`，0000 0001 1000 0010，高 13 位是 0x30 = 48，也就是 IDT 索引号是 48，这是 syscall，现在根本没实现，甚至没在 trapentry.S 里写，所以触发通用保护异常了。那就没问题

### Questions 2

> 3. breakpoint 测试案例要么会产生 break point 断点异常，要么是一般保护异常，这取决于你最初在 IDT 中怎样初始化断点入口（即通过 `trap_init` 中的 `SETGATE` 调用）。这是为何？要想让断点异常像上面指出的那样工作，你应当如何进行设置？如果是怎样的不正确设置又将会导致一般保护异常的触发？ 
> 4. 你认为这些机制有什么意义（特别是基于你在 `user/softint` 测试程序观察到的内容）？

3. 在 Exercise 6 和 Challenge 2 里面我们也发现了这种问题。这主要是我们把特权级设置为了 0，当我们以用户态去调用时，硬件会判定：有低特权级的代码非法调用高特权级的中断处理程序，CPU 将拦截该指令并抛出通用保护异常。所以，我们得在 trap_init() 中把 T_BRKPT 的 DPL 设置为 3.
4. 之前我们分析过，softint 会通过 $int 14 主动触发页错误异常，由于其 DPL 为 0，硬件拦截它并抛出 GPF。这种机制的意义在于将内核与用户态隔离，便于系统保护，防止用户态程序操纵内核。

## System calls

用户进程通过发起系统调用来请求内核替它们代劳执行任务。当用户进程激活系统调用时，处理器进入内核模式，处理器以及内核协同保存用户进程的上下文状态，内核接着执行有关代码来完成系统调用，之后恢复用户进程。至于究竟用户进程如何取得内核的关注、以及它是怎样指示自身想运行的调用操作的细节，随着系统的不同而产生变化。

在 JOS 内核里，我们将使用 `int` 指令来引起一次处理器中断。具体来说，我们将会把 `int $0x30` 使用为系统调用中断。我们已经提前替你把常量 `T_SYSCALL` 定义为了 48 (`0x30`)。你将必须设置好中断描述符以使能用户进程发起该中断。要注意的是中断 `0x30` 是不可能通过硬件生成的，所以在允许用户态代码生成它这件事上不存在歧义。

应用程序将通过寄存器传递系统调用编号和相关的参数。用这招，内核就不必在用户环境下的调用栈或指令流上大做文章。系统调用号将会放入 `%eax` 之中，各参数（至多五个）将按顺序分别放置在 `%edx`、`%ecx`、`%ebx`、`%edi` 以及 `%esi` 中。内核将返回值从 `%eax` 传递返回。汇编调用系统调用的代码已经为你写好了，存在 `lib/syscall.c` 里的 `syscall()`。你应该通读并确认明白正在发生些什么事。

### Exercise 7

> 在内核中添加专门服务中断向量 `T_SYSCALL` 的处理程序。你得编辑 `kern/trapentry.S` 和 `kern/trap.c` 的 `trap_init()`。你同时也需要修改 `trap_dispatch()` 以通过利用适当的参数调用 `syscall()`（定义在 `kern/syscall.c` 当中），来专门处理该系统调用中断，然后安排返回值放进 `%eax` 被传回给用户进程。最终，你需要在 `kern/syscall.c` 内实现 `syscall()`。确保当遇到无效调用号时 `syscall()` 会传回 `-E_INVAL`。你应该了解阅读并明确 `lib/syscall.c`（特别是当中的内联汇编例程）以确定你懂系统调用这个接口是如何通信的。通过利用相应的内核函数对应于每一项调用，应对处理所有处于 `inc/syscall.h` 之中的清单系统调用项。
>
> 使用你的内核（执行 `make run-hello`）运行 `user/hello` 程序。它应当向控制台输出 "hello, world" 然后发生页面出错故障进入用户模式。如果没有发生此事，多半可能你的系统调用处理例程还没调整正确。此时你同样也能看到通过对 `testbss` 进行的 `make grade` 能得以取得成功状态。

首先，kern/trapentry.S 补全`TRAPHANDLER_NOEC(syscall_entry, T_SYSCALL)`，然后 kern/trap.c 单独注册 syscall 的处理

```c
extern void syscall_entry();
SETGATE(idt[T_SYSCALL], 0, GD_KT, syscall_entry, 3);
```

然后是 trap_dispatch()，这里我们需要解析陷阱帧中的寄存器，把它们传递给处理函数，然后记得把结果写回 tf 的 %eax

```c
if (tf->tf_trapno == T_SYSCALL) {
    int32_t ret = syscall( // 注意寄存器顺序。eax 是调用号
        tf->tf_regs.reg_eax,
        tf->tf_regs.reg_edx,
        tf->tf_regs.reg_ecx,
        tf->tf_regs.reg_ebx,
        tf->tf_regs.reg_edi,
        tf->tf_regs.reg_esi
    );
    tf->tf_regs.reg_eax = ret;
    return;
}
```

最后是 kern/syscall.c，syscall() 也是一个 dispatcher，这里需要根据传入的系统调用号决定调用哪个函数。

```c
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

		default:
			return -E_INVAL;
	}
}
```

此时 make run-hello，确实是输出 hello world 后页错误

![[MIT 6.828 Lab 3] 5](img\[MIT 6.828 Lab 3] 5.png)

make grade 也能看见 testbss 通过检查

### Challenge 3

> 采用使用 `sysenter` 及 `sysexit` 替代 `int 0x30` 和 `iret` 去实装系统调用。 `sysenter/sysexit` 指令系由 Intel 设计以取代慢速且冗繁的 `int/iret` 操作。它们的优势得益于只使用寄存器而并非堆栈而且假定关于段寄存器是如何投入使用的。查考 Intel 参考指导册的 2B 册可了解详细情况。 要将其简便添加到 JOS 最简单的方式就是在于 `kern/trapentry.S` 中增加一名 `sysenter_handler` 其能保留住足够有关该用户环境以回归于彼的信息。并安置起内核的环境条件，推行起各项向向着 `syscall()` 传入的参量随后当面执行 `syscall()`。一旦 `syscall()` 做回反馈后，设定好一切必须件而后开通运用 `sysexit` 这项指令。你亦需要再另外向着 `kern/init.c` 追加写入用以布置必要的特定型寄存器 (MSRs) 所需代码。相关有关于诸类 MSR 之上佳阐明能往 AMD 架构的程序员指南或英特尔手册当中觅得指点。你在编写代码以增至 `inc/x86.h` 当写入用指令 `wrmsr` 时也可查寻实现的方法。
>
> 最后，必须通过变更 `lib/syscall.c` 方能借助于 `sysenter` 操作。可能的适用于指令 `sysenter` 寄存器排布方案：
>
> ```assembly
> 	eax                - syscall 编号
> 	edx, ecx, ebx, edi - arg1, arg2, arg3, arg4
> 	esi                - return pc
> 	ebp                - return esp
> 	esp                - 因 sysenter 遭清废
> ```
>
> GCC 内联汇编功能能够在你明确示意要求直接推入特定值情况下自动储备好各类寄存。只是别忘通过 `push` 进和恢复性 `pop` 操作另外几项被你踩踏覆辙抹灭的其他存放位或者明确通禀其相关内联性组装汇构你确实干扰乱序它们了。既然内敛性配置里并不支持保管 `%ebp` 的作业那这就代表得自己上手段保存兼回档这一件寄存内容了。返回地址借由像是 `leal after_sysenter_label, %%esi` 操作手段从而得以被放置。 得留意一下这种手法限定于仅能提供最多 4 条指令支持参量范围的援助，这也就是说您还是依旧保留维持用以承办具有到至极五条传入支持性参位数量级指令的那老式的传调法才行。除此之外，鉴由于通过这个“快速路段”并不涉及到给现时处环境当下的异常截网处理陷阱架位图来次补上换新内容，这么操作将会致使其难于迎合到我们后面几个相关操作项目实装引入的各条指令体系之内去发挥效用的境地。 由于接下来实验启开激活后同步性质之中断机能加入关系原因，可能会迫使你对先前作业过的诸此等项写过的代码做出修改调换；准确来说你须在回到使用者作业之阶段上进行将终端信号操作做给释放可开启，这操作本来就是因为 `sysexit` 自身就不自带这些代作功劳机制所在之缘由的。

这里就是在告诉我们，基于中断的系统调用还是太慢了，当执行 int 时，CPU 需要访问内存去查 IDT 表，需要访问 TSS 找内核栈指针，还要去做一串压栈。所以 Intel 引入了 sysenter 和 sysexit，绕过内存，直接通过 CPU 内部的 MSRs 寄存器操作。

这个 Challenge 我没通过 make grade，不知道是哪出问题了，下面把我的尝试写出来吧。

首先在 inc/x86.h 定义 MSR 地址以及写入它们的宏

```c
#define MSR_IA32_SYSENTER_CS  0x174
#define MSR_IA32_SYSENTER_ESP 0x175
#define MSR_IA32_SYSENTER_EIP 0x176

static inline void wrmsr(uint32_t msr, uint64_t val) {
    asm volatile (
        "wrmsr"
        : // 无输出操作数
        : "c" (msr),                       // 输入: 将 msr 放入 ecx
          "a" ((uint32_t)val),             // 输入: 将 val 的低 32 位放入 eax
          "d" ((uint32_t)(val >> 32))      // 输入: 将 val 的高 32 位放入 edx
    );
}
```

然后 kern/init.c，在 i386_init() 的 trap_init() 后面写 msr 配置代码，记得开头写头文件`#include <inc/x86.h>`

```c
	// Lab 3 Challenge 3
	extern void sysenter_entry();
	// 内核代码段选择子
	wrmsr(MSR_IA32_SYSENTER_CS, GD_KT);
	// 内核栈顶指针
	wrmsr(MSR_IA32_SYSENTER_ESP, KSTACKTOP);
	// 系统调用入口地址
	wrmsr(MSR_IA32_SYSENTER_EIP, (uint32_t)sysenter_entry);
```

lib/syscall.c 改成

```c
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
```

kern/trapentry.S 的末尾加上

```assembly
.globl sysenter_entry
.type sysenter_entry, @function
.align 2
sysenter_entry:
    # 此时，硬件已经将 esp 设为了 KSTACKTOP，CS 设为了 GD_KT。
    # 寄存器中包含 eax (num), edx (a1), ecx (a2), ebx (a3), edi (a4)
    # esi (用户态返回 eip), ebp (用户态返回 esp)
    
    # 准备调用 kern/syscall.c 的 syscall()
    pushl $0       # a5
    pushl %edi     # a4
    pushl %ebx     # a3
    pushl %ecx     # a2
    pushl %edx     # a1
    pushl %eax     # syscallno
    
    call syscall
    
    # 清理参数栈，6 个 32 位参数 = 24 字节
    addl $24, %esp
    

    # sysexit 指令要求返回的 eip 必须在 %edx 中，返回的 esp 必须在 %ecx 中。
    movl %esi, %edx
    movl %ebp, %ecx
    
    # sysexit 不会自动开启中断，我们需要在返回用户态前手动开中断
    sti 
    
    sysexit
```

最后 make grade 会

```shell
testbss: FAIL (2.0s) 
    AssertionError: ...
         check_page_installed_pgdir() succeeded!
         [00000000] new env 00001000
    GOOD Making sure bss works right...
         Incoming TRAP frame at 0xefffffc0
         TRAP frame at 0xefffffc0
    ...
           esp  0x00000023
           ss   0x----ff53
    GOOD [00001000] free env 00001000
         Destroyed the only environment - nothing more to do!
         Welcome to the JOS kernel monitor!
         Type 'help' for a list of commands.
         qemu: terminating on signal 15 from pid 11808
    MISSING 'Yes, good.  Now doing a wild write off the end...'
    MISSING '.00001000. user fault va 00c..... ip 008.....'
    
    QEMU output saved to jos.out.testbss

```

Making sure bss works right 说明 sys_cputs 其实是正常的，但是最后检查 .bss 的大数组时崩溃了，我检查了一下，之前 kern/env.c 的 load_icode() 我也把多出来的那部分内存正常清零了，没想明白是哪出问题了。

## User-mode startup

用户程序从 `lib/entry.S` 的顶部开始运行。经过一些设置后，此代码调用 `lib/libmain.c` 中的 `libmain()`。你需要修改 `libmain()`，初始化全局指针 `thisenv` 以指向 `envs[]` 数组中对应此环境的 `struct Env`。（注意：`lib/entry.S` 已经将 `envs` 定义为指向你在 A 部分设置的 `UENVS` 映射了。）提示：查看 `inc/env.h` 并使用 `sys_getenvid`。

`libmain()` 然后会调用 `umain`。在 `hello` 程序的情况下，它位于 `user/hello.c`。请注意，在打印完 "hello, world" 之后，它试图访问 `thisenv->env_id`。这就是为什么它之前会发生异常错误。既然你已经正确地初始化了 `thisenv`，它应该就不会再抛出异常了。如果它仍然报错，你可能尚未将 `UENVS` 区域映射为用户可读（回想第一部分的 `pmap.c`；这是我们第一次真正使用 `UENVS` 区域）。

### Exercise 8

> 向用户库中添加所需代码，然后启动你的内核。你应该会看到 `user/hello` 打印出 "hello, world"，随后打印 "i am environment 00001000"。然后 `user/hello` 会尝试通过调用 `sys_env_destroy()`（参见 `lib/libmain.c` 和 `lib/exit.c`）来“退出”(exit)。由于内核目前仅支持单个用户环境，所以它应当报告它已经销毁了唯一的一个环境，随后陷入内核监视器。此时你应能在 `hello` 测试中获得 `make grade` 成功。

这里主要是完善用户态启动代码。根据 lib/libmain.c 的注释，我们在 Exercise 7 实现过 sys_getenvid，它可以返回 env_id，inc/env.h 里提供了宏 ENVX(envid)，用来从 env_id 提取出该环境在 envs 数组中的索引

我们在 Part A 的时候把 envs 映射到了用户态的 UENVS，所以直接读取就行

```c
void
libmain(int argc, char **argv)
{
	// set thisenv to point at our Env structure in envs[].
	// LAB 3: Your code here.
	// thisenv = 0;
	thisenv = &envs[ENVX(sys_getenvid())];

	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}
```



![[MIT 6.828 Lab 3] 6](img\[MIT 6.828 Lab 3] 6.png)

## Page faults and memory protection

内存保护是一个操作系统的关键特性，确保一个程序中的错误无法损坏其他程序或破坏操作系统自身。 操作系统通常依赖硬件支持来实现内存保护。操作系统通知硬件关于哪些虚拟地址是合法的，哪些是不合法的。当一个程序试图访问一个非法的或者它没有权限访问的地址时，处理器会在引发错误的指令处停止程序的执行，然后将错误信息陷入内核。如果此错误可修复，内核可以修复它并让程序继续运行。如果此错误无法修复，那么程序就无法继续执行了，因为它永远无法越过引发错误的指令。

作为可修复错误的例子，考虑一个自动扩展的栈。在许多系统中，内核最初仅分配一个单页的栈，然后如果程序向栈更深处的地址访问触发了缺页异常，内核会自动分配那些页面并让程序继续。通过这种做法，内核仅仅分配了程序需要的内存，但在程序看来，它仿佛拥有了一个任意大小的栈。

而在系统调用中，内存保护引出了一个有趣的问题。多数系统调用接口允许用户程序传递指针给内核。这些指针指向等待被读取或写入的用户缓冲区。然后内核在执行系统调用的过程中会解引用这些指针。这带来了两个问题：

1. 内核中的缺页异常远比在用户程序中的缺页异常严重。如果内核在操作其自身数据结构时发生缺页，那说明存在内核 Bug，且此错误处理应直接让内核 panic（从而使整个系统停止）。但是当内核解引用由用户提供的指针时，它需要有一种机制能记住由于这种解引用引发的缺页异常实际是在替用户程序办事（而不是内核的 Bug）。
2. 内核通常拥有比用户层程序更高的内存读写权限。用户程序可能会将指向系统调用指针，指向一块内核有权限读写但用户程序自身无法读写的内存。内核必须极其小心地确保不会被欺骗去解引用这种指针，因为这有可能会泄露私密信息或破坏内核的完整性。

由于这两个原因，内核在处理从用户空间传入的指针时，必须非常谨慎。 现在你将使用单一的机制（审查所有从用户空间传入内核的指针）来解决这两个问题。当一个程序传递给内核一个指针时，内核将核实该地址是否在用户层的地址空间内，并且验证页表是否允许内存操作。 因此，内核绝不应该因为解引用了一个用户提供的指针而遭受缺页异常。如果内核确实发生了缺页异常，它应当直接发生 panic 并终止。

### Exercise 9

> 修改 `kern/trap.c`，如果在内核模式下发生缺页异常，使其直接发生 panic 宕机。 提示：若要判断异常发生在用户模式还是内核模式，请检查 `tf_cs` 的低位比特。
>
> 阅读 `kern/pmap.c` 中的 `user_mem_assert`，在同一文件中实现 `user_mem_check`。 更改 `kern/syscall.c` 来对传递给系统调用的参数进行健全性检查。
>
> 启动你的内核运行 `user/buggyhello`。该环境应当会被销毁，而且内核不应该发生 panic。你应该能看到： `[00001000] user_mem_check assertion failure for va 00000001` `[00001000] free env 00001000` `Destroyed the only environment - nothing more to do!`
>
> 最后，更改 `kern/kdebug.c` 中的 `debuginfo_eip`，使其在 `usd`、`stabs` 和 `stabstr` 上调用 `user_mem_check`。如果你现在运行 `user/breakpoint`，你应该能够从内核监视器运行 `backtrace` 并看到 backtrace 遍历回 `lib/libmain.c`，随后内核才因缺页异常而 panic。是什么引发了这一缺页异常？你无需修复它，但你应该理解其原因。

分成四步做

首先 kern/trap.c，page_fault_handler 中，根据 tf->tf_cs 的低二位 CPL，我们可以判断发生缺页时 CPU 是处于内核态还是用户态。如果是内核态，就 panic。

```c
void
page_fault_handler(struct Trapframe *tf)
{
	uint32_t fault_va;

	// Read processor's CR2 register to find the faulting address
	fault_va = rcr2();

	// Handle kernel-mode page faults.

	// LAB 3: Your code here.
	if ((tf->tf_cs & 3) == 0) {
		panic("page fault in kernel mode, fault_va: 0x%08x", fault_va);
	}
	// We've already handled kernel-mode exceptions, so if we get here,
	// the page fault happened in user mode.

	// Destroy the environment that caused the fault.
	cprintf("[%08x] user fault va %08x ip %08x\n",
		curenv->env_id, fault_va, tf->tf_eip);
	print_trapframe(tf);
	env_destroy(curenv);
}

```

然后 kern/pmap.c，根据注释，user_mem_check 会给定一段起始地址 va 和长度 len，逐页检查这段内存是否属于用户空间（小于 ULIM），并且是否在页表中拥有 PTE_P 以及参数要求的 perm 权限。

注意，如果跨越了页边界，且前面合法后面非法，需要把第一个非法地址记录在 user_mem_check_addr 中

```c
int
user_mem_check(struct Env *env, const void *va, size_t len, int perm)
{
	// LAB 3: Your code here.
	uintptr_t start = (uintptr_t) va;
	uintptr_t end = start + len;
	uintptr_t i;

	for (i = start; i < end; i = ROUNDDOWN(i + PGSIZE, PGSIZE)) { // 确保下一次循环严格从页边界开始
		pte_t *pte = pgdir_walk(env->env_pgdir, (void *)i, 0);
		
		// 址是否超出了用户空间上限、页表项是否存在、是否对该页有相应的权限
		if (i >= ULIM || !pte || !(*pte & PTE_P) || (*pte & perm) != perm) {
			user_mem_check_addr = i;
			return -E_FAULT;
		}
	}

	return 0;
}
```

然后是 kern/syscall.c，在 sys_cputs 中加上安全检查

```c
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
```

make run-buggyhello，可以看见 user_mem_check 的检查

![[MIT 6.828 Lab 3] 7](img\[MIT 6.828 Lab 3] 7.png)

最后一步，kern/kdebug.c 的 debuginfo_eip 中对三处解引用添加检查

```c
const struct UserStabData *usd = (const struct UserStabData *) USTABDATA;

// Make sure this memory is valid.
// Return -1 if it is not.  Hint: Call user_mem_check.
// LAB 3: Your code here.
if (user_mem_check(curenv, usd, sizeof(struct UserStabData), PTE_U) < 0) {
    return -1;
}

stabs = usd->stabs;
stab_end = usd->stab_end;
stabstr = usd->stabstr;
stabstr_end = usd->stabstr_end;

// Make sure the STABS and string table memory is valid.
// LAB 3: Your code here.
if (user_mem_check(curenv, stabs, stab_end - stabs, PTE_U) < 0) {
    return -1;
}
if (user_mem_check(curenv, stabstr, stabstr_end - stabstr, PTE_U) < 0) {
    return -1;
}
```

然后 make run-breakpoint，这里顺便发现之前手贱把 kern/monitor.c 里 backtrace 的注册项删了，补回来才能用 backtrace

![[MIT 6.828 Lab 3] 8](img\[MIT 6.828 Lab 3] 8.png)

这里就能看见 kernel panic 了。因为 backtrace 是一个 r0 程序，它通过不断解引用 ebp 来沿着栈帧链回溯。当我们从用户态的 int3 陷入内核时，最初的 ebp 是用户态的栈指针，backtrace 会顺一路回溯到 user/hello.c，再回溯到 lib/libmain.c。

但是，用户态的栈有限，在 libmain 之前没有更早的有效栈帧，此时再解引用 ebp，就会读到没映射的用户态内存。由于此时 CPU 处于内核态，就触发了我们在 page_fault_handler 内写的 panic。

这也再次提醒我们，内核不能随意解引用来自用户空间的指针，必须确保其在合法用户栈范围内。

---

请注意，你刚刚实现的相同机制对于恶意的用户程序（诸如 `user/evilhello`）也有效。

### Exercise 10

> 启动内核运行 `user/evilhello`。该环境应当被销毁，且内核不发生 panic。你应该会看到： `[00000000] new env 00001000` `...` `[00001000] user_mem_check assertion failure for va f010000c` `[00001000] free env 00001000`

![[MIT 6.828 Lab 3] 9](img\[MIT 6.828 Lab 3] 9.png)

没问题

---

![[MIT 6.828 Lab 3] 10](img\[MIT 6.828 Lab 3] 10.png)

Lab 3 done
