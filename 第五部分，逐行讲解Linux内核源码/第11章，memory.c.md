# 1.memory.c的总体功能

## 1.1 memory.c的总体功能
memory.c 程序 进行**内存分页的管理**。实现了对主内存区内存的**动态分配**和**收回**操作。对于物理内存的管理，内核使用了一个**字节数组（mem_map[]）**来表示**主内存区中所有物理内存页**的状态。每个字节描述一个物理内存页的占用状态。 其中的值表示被占用的次数，0表示对应的物理内存空闲着。 当申请一页物理内存时，就将对应字节的值增1。 对于进程虚拟线性地址的管理，内核使用了**处理器**的**页目录表**和**页表结构**来管理。而物理内存页与进程线性地址之间的映射关系则是通过**修改页目录项和页表项的内容**来处理。

## 1.2 memory.c的函数功能分析
memory.c位于[memory.c](../src/mm/memory.c)
里面的如下函数，对应的功能如下：

一共有19个函数：

|memory.c函数|对应功能|
|-|-|
|void oom(void)|xxx|
|||
|||
|||

### 1.2.1 static inline void oom(void)

>作用：**内存耗尽**（Out Of Memory）处理函数，当系统无法分配所需内存时被调用，直接打印 "Out of memory" 并调用do_exit()函数退出该进程，完成资源释放，并通知父进程完成资源回收，返回SIGSEGV信号。

>SIGSEGV信号=> “段错误”，进程访问了**非法内存地址**（如空指针 dereference）。

```c
void do_exit(long code);
static inline void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}
```

显然，这里主要是printk()函数和do_exit()函数的实现。
因为两者的实现都较为复杂，所以我们对其实现知识屏蔽，只关注其功能。这样有利于我们专注梳理memory的功能。

这两个函数我们后面会着重逐行代码讲解，并在使用到他们的地方贴上讲解链接。所以不用担心。

- printk()

功能仅仅为打印

- do_exit()

主要负责处理**进程的退出逻辑**，是**进程终止时的核心入口**：它会释放进程占**用的各类系统资源**（**内存**、**文件句柄**、**文件系统上下文**、**终端 / 会话资源**等），处理**子进程的父进程继承**问题，将当前进程标记为 “**僵尸态（TASK_ZOMBIE）**” 并保存退出码，通知**父进程回收自身资源**，最后**触发调度器切换到其他进程执行**。

### 1.2.2 invalidate()

>作用：刷新页表，将cr3寄存器的值设置为0。强制刷新 CPU 的**地址转换缓存**（**TLB**，Translation Lookaside Buffer），让 **CPU 丢弃缓存的旧页表映射关系**，后续内存访问必须**重新**从页目录 / 页表中**读取最新的映射**，确保页表修改（如页表项更新、释放、拷贝）后**地址映射的正确性**。

```c
#define invalidate() \
        __asm__( \
        "movl %%eax,%%cr3" \
        ::\
        "a" (0))
```
这是GNU内联汇编的写法，**从：：为分界线**，：：前面为操作，：：后面为赋值。CPU先执行：：后面的赋值行为。

#### 1.2.2.1 赋值行为：
- "a" (0)
"a"表示eax寄存器，"a" (0)表示将0赋值给eax寄存器。

#### 1.2.2.2 操作行为：
- "movl %%eax,%%cr3"
表示，将eax寄存器的值，写入cr3寄存器。eax前面被赋值为0了，所以这里表示将0写入cr3寄存器，从而刷新页表。

### 1.2.3 MAP_NR(addr)
>作用：将线性地址转换为物理页面编号。

```c
#define LOW_MEM 0x100000
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)
```
这里我们可以看到，
- 0x100000下面是内核，我们不管。
- 每个**页面是4KB**，所以**右移12位就是除以4KB，得到页面编号**。


### 1.2.4 CODE_SPACE(addr) 

>作用：判断线性地址是否在代码空间。

```c
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)
```

#### 1.2.4.1 进程任务结构体讲解

这里的`current->start_code`和`current->end_code`分别是指啥？
首先，current是指的当前进程的任务结构体。具体实现如下：

```c
struct task_struct {
/* these are hardcoded - don't touch */
	long state;	    /* -1 unrunnable, 0 runnable, >0 stopped */
                    //进程状态：标记进程当前的运行状态，决定调度器是否可选择该进程
	long counter;   //剩余时间片，实现时间片轮转调度的核心字段
	long priority;  //优先级。优先级越高，初始时间片越长
	long signal;    //未处理信号位图：标记进程收到但尚未处理的信号
	struct sigaction sigaction[32];//信号处理动作数组：定义每个信号的“处理方式”
	long blocked;	/* bitmap of masked signals */
                    //阻塞信号掩码：标记当前“被阻塞”的信号（被阻塞的信号不会触发处理，暂存于 signal 中）

/* various fields */
	int exit_code;  //进程退出码：存储进程的终止状态，供父进程回收。
                    //父进程通过 wait()/waitpid() 系统调用读取该值，判断子进程的终止原因。
	unsigned long start_code,end_code,end_data,brk,start_stack;
                    //进程内存区域边界：标记进程在内存中的代码段、数据段、栈段等关键地址，用于内存管理
                    //start_code，代码段起始地址；end_code，代码段长度；
                    //end_data，数据段长度；brk，堆段当前末尾地址；start_stack，用户栈起始地址。
	long pid,father,pgrp,session,leader;
                    //进程ID与亲属关系：``标识进程唯一``性及进程间的父子/组关系，用于进程管理
	unsigned short uid,euid,suid;
                    //用户身份标识：控制进程的文件访问权限、系统资源访问权限（Unix 安全模型核心）
	unsigned short gid,egid,sgid;
                    //用户组身份标识：控制进程的文件访问权限、系统资源访问权限（Unix 安全模型核心）
	long alarm;     //闹钟定时器：记录进程设置的闹钟时间，用于实现 alarm() 系统调用
	long utime,stime,cutime,cstime,start_time;
                    //进程时间统计：记录进程的 CPU 使用时间，用于资源统计和调度优化
	unsigned short used_math;
                    //数学协处理器使用标记：标记进程是否使用 80387 数学协处理器（早期 x86 架构专用）

/* file system info */
	int tty;		            //进程关联的终端设备号：标记进程对应的终端（如控制台、串口）
	unsigned short umask;       //文件创建掩码：控制新创建文件的默认权限（屏蔽指定的权限位）
	struct m_inode * pwd;       //进程当前工作目录的 inode 指针：指向当前工作目录的索引节点（inode）
	struct m_inode * root;      //进程根目录的 inode 指针：指向进程“根目录”的索引节点（默认是系统根目录 /）
	struct m_inode * executable;//进程可执行文件的 inode 指针：指向当前进程运行的可执行文件的索引节点
	unsigned long close_on_exec;//执行 exec() 时需关闭的文件掩码：标记进程中执行 exec() 系统调用后需自动关闭”的文件
	struct file * filp[NR_OPEN];//文件描述符表：存储进程打开的文件的指针数组，是进程访问文件的核心接口

/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];  //进程的局部描述符表,
    //  * 数组结构（3 个描述符，固定用途，不可修改）：
    //  * - ldt[0]：空描述符（x86 要求 LDT 第一个描述符必须为 0，用于容错）；
    //  * - ldt[1]：代码段描述符（CS 段寄存器指向该描述符，定义进程代码段的基地址、限长、权限）；
    //  * - ldt[2]：数据段/栈段描述符（DS、SS 段寄存器指向该描述符，定义数据段/栈段的内存属性）；

/* tss for this task */
	struct tss_struct tss;      //进程的任务状态段，x86架构进程切换核心
    //  * 核心逻辑：
    //  * - 进程切换时，内核将当前进程的寄存器状态保存到其 TSS 中；
    //  * - 加载新进程时，从其 TSS 中恢复寄存器状态，实现“上下文切换”；
};
```

#### 1.2.4.2 页面对齐
这里的`((addr)+4095)&~4095`是啥意思呢？

这里的`((addr)+4095)&~4095`的作用是将地址`addr`对齐到4096字节（即一页）的**边界**。
具体实现是将`addr`加上4095，然后将结果与0x00000fff的补码按位取反，即使之能够被4096整除。完成页面的对齐。
这样做的原因是，4096字节是一页的大小，对齐到页边界可以提高内存访问的效率。

#### 1.2.4.3 判断地址空间是否合法
`((((addr)+4095)&~4095) < current->start_code + current->end_code)`的作用是判断地址`addr`是否在当前进程的代码空间内。如果上述小于成立，则是合法的地址空间。

所以，这里的`current->end_code`是指的进程的**代码空间的长度**，否则这里说不过去了。

### 1.2.5 copy_page(from,to) 
####  1.2.5.1 代码实现
代码实现如下：
```c
#define copy_page(from,to) \
    __asm__(
        "cld ; 
        rep ; 
        movsl"//一个双字，即4字节.
        ::
        "S" (from),
        "D" (to),
        "c" (1024)//1024个4字节，即4KB.
    )
```

#### 1.2.5.2 作用
> 拷贝整个页面，即把一个 `4KB 大小`的`内存页`从 `from 地址`拷贝到 `to 地址`。

#### 1.2.5.3 实现原理

- `cld`：清除方向标志，确保字符串操作指令（如 movsl）从**低地址向高地址**进行复制。

- `rep movsl`：重复执行 **movsl 指令**，直到**计数器（ecx）为 0**。每次 movsl 会**复制 4 字节（一个双字）**，所以当 ecx 初始化为 1024 时，表示要复制 1024 个双字，即 4096 字节（4KB）。

- `"S" (from)`：将 `from` 地址加载到 **esi 寄存器**，作为**源地址**。

- `"D" (to)`：将 `to` 地址加载到 **edi 寄存器**，作为**目的地址**。

- `"c" (1024)`：将 1024 加载到 **ecx 寄存器**，作为**复制的双字数量**。

> 由此看出，这就是C中的嵌入式汇编语句的威力。实质上，**这是gcc的标准，专门用来这么实现的！**

### 1.2.6 unsigned long get_free_page(void)
#### 1.2.6.1 作用
> 从**内存**中分配一个**空闲页面（物理页）**，并将其**标记为已用**。如果没有空闲页面可用，则返回 0。

>本质上是从内存管理的 `mem_map` 数组中**查找并返回**一个**空闲页面的物理地址**。

#### 1.2.6.3 代码实现
```c
/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
// 函数注释：获取第一个（实际是最后一个）空闲页的物理地址，标记为已用，无空闲返回0
unsigned long get_free_page(void)
{
    // 1.绑定寄存器
register unsigned long __res asm("ax");//这里是一个寄存器eax，用来存储返回值

    // 2. 内嵌汇编核心逻辑
__asm__(
    "std ;     //std：设置方向标志位（DF），使字符串操作指令按从高地址到低地址的方向进行。

    repne ;     //repne：重复执行后面的指令，直到ZF标志位为 1 或者计数器CX为 0。

    scasb\n\t"  //scasb：从ES:[EDI]指向的内存单元中读取一个字节，并与AL寄存器中的值进行比较，设置标志位。这里是在扫描mem_map数组（EDI指向mem_map + PAGING_PAGES - 1）。

	"jne 1f\n\t"//如果上一条scasb指令执行后ZF标志位不为 1（即没有找到值为 0 的字节），则跳转到标号1处（也就是没有空闲页面的情况）。

	"movb $1,1(%%edi)\n\t"//如果找到了值为 0 的字节（表示找到空闲页面），将edi指向的下一个字节（mem_map中对应页面的标志位）设置为 1，表示该页面已被使用。

	"sall $12,%%ecx\n\t"//将cx寄存器的值左移 12 位。因为页面大小是 4096 字节（2 的 12 次方），这里是计算页面的物理地址偏移量。

	"addl %2,%%ecx\n\t"//将LOW_MEM（第二个输入操作数）加到cx寄存器中，得到页面的物理地址的一部分。

	"movl %%ecx,%%edx\n\t"//将cx寄存器的值复制到edx寄存器中，保存页面的物理地址的一部分。

	"movl $1024,%%ecx\n\t"//将cx寄存器设置为 1024，可能是用于后续的循环操作（这里可能是填充页面相关的操作）。

	"leal 4092(%%edx),%%edi\n\t"//计算edi的值为edx + 4092，可能是指向要填充的页面的起始位置。

	"rep ; stosl\n\t"//重复执行stosl指令，将eax寄存器中的值（这里应该是 0，因为没有明确设置其他值）存储到ES:[EDI]指向的内存区域中，共执行cx（1024）次，可能是用于初始化页面内容。

	" movl %%edx,%%eax\n"//将edx寄存器中的值（页面的物理地址的一部分）复制到eax寄存器中，作为函数的返回值（__res绑定到ax）。

	"1: cld"//标号1处，清除方向标志位（DF），恢复字符串操作的默认方向（从低地址到高地址）。
    //输出约束
	:
    "=a" (__res)//指定eax寄存器（对应__res变量）作为输出。
    // 输入约束：
    :
    "0" (0),// "0" (0)：将常数 0 作为第一个输入操作数=
    "i" (LOW_MEM),// "i" (LOW_MEM)：将LOW_MEM作为立即数输入。
    "c" (PAGING_PAGES),// "c" (PAGING_PAGES)：将PAGING_PAGES作为cx寄存器的输入。
	"D" (mem_map+PAGING_PAGES-1)// "D" (mem_map + PAGING_PAGES - 1)：将mem_map + PAGING_PAGES - 1作为edi寄存器的输入。

	);
    return __res;//返回计算得到的空闲页面的物理地址（如果有的话），如果没有空闲页面则返回 0（因为在jne 1f分支中没有设置__res的值，所以默认是 0）。

}
```

### 1.2.7 void free_page(unsigned long addr)

#### 1.2.7.1 作用

> 释放**物理页** `addr`，并将其**标记为空闲**。如果 `addr` 不是**有效物理页地址**，则**panic**。

#### 1.2.7.2 代码实现
```c
/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;//检查要释放的页面地址是否小于LOW_MEM（低内存边界）。如果是，说明该地址不在可管理的内存范围内，直接返回，不进行任何操作。

	if (addr >= HIGH_MEMORY)//检查要释放的页面地址是否大于等于HIGH_MEMORY（高内存边界）。是的话，直接panic

		panic("trying to free nonexistent page");

    //两个范围内的都排除了，剩下的，就是正确范围中的了！
	addr -= LOW_MEM;//如果页面地址在有效范围内（大于等于LOW_MEM且小于HIGH_MEMORY），将地址减去LOW_MEM，得到相对于低内存起始地址的偏移量。

    //将偏移量右移 12 位。因为页面大小是 4096 字节（2 的 12 次方），这样做是为了得到该页面在mem_map数组中的索引。
	addr >>= 12;
    //检查mem_map数组中对应索引位置的页面引用计数,统一减少。如果减少前不为0，那么直接return。
	if (mem_map[addr]--) return;
    //如果减少前为0，那么此时就为-1了，不应该，所以这里需要重新设置为0.并在下面的panic中提示错误。表示释放了一个已经空闲的页面。
	mem_map[addr]=0;
	panic("trying to free free page");
}
```
### 1.2.8 int free_page_tables(unsigned long from,unsigned long size)
#### 1.2.8.1 作用
>释放从指定**起始地址from**开始的连续的**size个页表**及其对应的页面。

#### 1.2.8.2 代码实现
```c
/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;//pg_table用于指向当前要释放的页表。
	unsigned long * dir, nr;//dir指向页目录项，nr表示循环计数。

    //检查起始地址from的低 22 位是否全为 0（0x3fffff是低 22 位全为 1 的掩码），如果不是则说明地址对齐不正确，调用panic函数并输出错误信息。
    //为啥是22位，为啥要对齐？
    //22位是因为2^22=4MB,每个页表项占用4字节，每个页表有1024个页表项，所以每个页表占用4MB。所以，这里检查对齐是为了确保from是4MB的整数倍。

	if (from & 0x3fffff) 
        //对齐出错
		panic("free_page_tables called with wrong alignment");
	if (!from)
        //如果from为0，说明没有要释放的页表了，直接返回0。
		panic("Trying to free up swapper memory space");
    size = (size + 0x3fffff) >> 22;
    //将大小size加上0x3fffff后右移 22 位，这是为了将大小转换为以 4MB 块为单位（因为 4MB = 2^22 字节）。
    //相当于除以4MB[向下取整]，得到要释放的页表数量。
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
    //为啥是from>>20，为啥是0xffc？
    //0xffc是111111111100，用来做与操作，将低 2 位清零，保证地址对齐，同时保留页目录索引部分。
    //这里为啥是这样做？((from>>20) & 0xffc)?
    //线性地址通常被分为三个部分：页目录索引（10 位）、页表索引（10 位）和页内偏移（12 位），总共 32 位。右移 20 位（from >> 20）可以得到页目录索引部分+多出2位用来做对齐。
    /*
    32位线性地址转化为页表地址的结构
    |*12位*|*10位*|*10位*|
    即
    |*页目录/页表索引*|*页表项索引*|*页内偏移*|
    */
    //通过与 0xffc 与操作，将低 2 位清零，保证地址对齐，同时保留页目录索引部分。
    //最终得到的是页目录项dir的物理地址的4倍。

	for ( ; size-->0 ; dir++) {//开启循环，每次循环后size减1，dir指针指向下一个目录项。
		if (!(1 & *dir))
            continue;
        //检查当前页表项的值的最低位是否为 1，如果是则说明该页表项对应的页面已分配，调用free_page函数释放该页面（通过与0xfffff000进行与操作得到页面的物理地址）。如果最低位不是 1，则说明该页表项未分配，直接继续下一次循环。
        //注意，*dir得到项结构是：
        /*
                    页目录项格式：
            31                    12 11    9 8 7 6 5 4 3 2 1 0
            +----------------------+--------+-+-+-+-+-+-+-+-+-+
            | 页表物理地址高20位    | AVL   |0|0|0|A|0|0|U|W|P|
            +----------------------+--------+-+-+-+-+-+-+-+-+-+
        */
        //这里最后一位就是P位，用来表示页面是否被加载到内存中。如果为1，说明页面已加载，需要释放。
		pg_table = (unsigned long *) (0xfffff000 & *dir);
        //如果目录项有效，从目录项中提取页表的物理地址（通过与0xfffff000进行与操作），并将其转换为指针类型赋给pg_table。得到高20位，即页表的物理地址高20位。

		for (nr=0 ; nr<1024 ; nr++) {   //开始一个内层循环，用于遍历页表中的 1024 个项（因为每个页表项对应一个页面）。
			if (1 & *pg_table)          //检查当前页表项的值的最低位是否为 1，如果是则说明该页表项对应的页面已分配，调用free_page函数释放该页面（通过与0xfffff000进行与操作得到页面的物理地址）。
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;              //将当前页表项的值设为0，即清空该页表项。
			pg_table++;                 //将当前页表项设置为 0，表示该页面已释放或未使用。
		}
		free_page(0xfffff000 & *dir);   //释放页目录项对应的页面。
		*dir = 0;                      //将当前页目录项的值设为0，即清空该页目录项。
	}
	invalidate();//调用invalidate函数，可能用于使缓存等无效，确保内存状态的一致性。
	return 0;
}
```
### 1.2.9 int copy_page_tables(unsigned long from,unsigned long to,long size)
#### 1.2.9.0 函数功能

>复制从指定**起始地址from**开始的连续的**size个页表及其对应的页面**到以**to地址**为起始地址的**连续内存区域。**

#### 1.2.9.1 代码实现
```c
/*
1、这是内存管理（mm）中最复杂的函数之一，它通过仅复制页面来复制一系列线性地址。
2、注意！我们不是复制任意内存块，地址必须能被 4MB 整除（一个页目录项），这使函数更简单。反正它只被fork使用。
3、注意 2！！当from等于 0 时，我们在第一次fork时复制内核空间。我们不想复制一整个页目录项，因为那会导致严重的内存浪费，我们只复制前 160 页 - 640kB。即便这比我们实际需要的多，但它不会占用更多内存，因为在低 1MB 范围内我们不使用写时复制，所以这些页面可以与内核共享。这就是nr为特定值的特殊情况。
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
                    //源线性地址from、目标线性地址to和要复制的内存范围大小
{
	unsigned long * from_page_table;//声明一个指向无符号长整型的指针，用于指向源页表。
	unsigned long * to_page_table;  //声明一个指向无符号长整型的指针，用于指向目标页表。
	unsigned long this_page;        //声明一个无符号长整型变量，用于临时存储当前处理的页面相关信息。
	unsigned long * from_dir, * to_dir;//声明两个指向无符号长整型的指针，分别用于指向源页目录和目标页目录。
	unsigned long nr; //声明一个无符号长整型变量，用于循环计数等用途。

	if ((from&0x3fffff) || (to&0x3fffff))
        //检查源地址from和目标地址to的低 22 位是否全为 0（0x3fffff是低 22 位全为 1 的掩码），如果不是则说明地址对齐不正确，调用panic函数并输出错误信息。
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
    //得到源页目录项的物理地址的4倍，将其转换为指针类型赋给from_dir。
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
    //得到目标页目录项的物理地址的4倍，将其转换为指针类型赋给to_dir。
	size = ((unsigned) (size+0x3fffff)) >> 22;
    //将要复制的内存范围大小(size)右移 22 位，得到要复制的页目录项数量。
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir)
            //检查目标页目录项对应的最低位是否为 1，如果是，说明目标位置已经存在内容，调用panic函数并输出错误信息。
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
            //检查源页目录项对应的最低位是否为 0，如果是，说明源页目录项对应的页表不存在或未使用，直接继续下一次循环。
			continue;
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);//得到高20位
        //从源页目录项中提取源页表的物理地址（通过与0xfffff000进行与操作），并将其转换为指针类型赋给from_page_table。
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
        //调用get_free_page函数分配一个新的页面作为目标页表，如果分配失败（返回0），则返回 -1 表示内存不足。
		*to_dir = ((unsigned long) to_page_table) | 7;
        //将分配的目标页表的物理地址与标志位（7，表示页面存在、可读写、用户访问）进行按位或运算，并将结果赋值给目标页目录项，表示该页目录项现在指向一个有效的页表。
        /*
                    页目录项格式：
            31                    12 11    9 8 7 6 5 4 3 2 1 0
            +----------------------+--------+-+-+-+-+-+-+-+-+-+
            | 页表物理地址高20位    | AVL   |0|0|0|A|0|0|U|W|P|
            +----------------------+--------+-+-+-+-+-+-+-+-+-+
        */		
        nr = (from==0)?0xA0:1024;//第一个是16进制的10*16=160页，第二个是1024页
        //如果源地址from为0（特殊情况，第一次fork时复制内核空间），则设置nr为0xA0（160），否则设置nr为1024（正常情况下每个页表有1024个页表项）。
        //这里页一目了然了，如果是第一个页目录项，我们只复制前 160 页 - 640kB。剩下的全部复制完成的页表4MB。
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {//同时from_page_table和to_page_table指针分别指向下一个源页表项和目标页表项。
			this_page = *from_page_table;   //将当前源页表项的值赋给this_page变量。
			if (!(1 & this_page))           //检查源页表项对应的最低位是否为 0，如果是，说明该页表项对应的页面不存在或未使用，直接继续下一次循环。
				continue;
			this_page &= ~2;                //将this_page的第 1 位（写保护位）清零，表示该页面暂时可读写。
			*to_page_table = this_page;     //将修改后的this_page值赋给目标页表项，表示目标页表项现在指向与源页表项相同的页面，但暂时不受写保护。
             /*
                        页表项格式：
                31                    12 11    9 8 7 6 5 4 3 2 1 0
                +----------------------+--------+-+-+-+-+-+-+-+-+-+
                | 页面物理地址高20位     | AVL   |0|0|0|A|0|W|U|P|
                +----------------------+--------+-+-+-+-+-+-+-+-+-+
            */

			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;//右移 12 位，得到该页面在mem_map数组中的索引。
				mem_map[this_page]++;//将mem_map数组中对应页面的引用计数加1。
			}
		}
	}
	invalidate();//调用invalidate函数，可能用于使缓存等无效，确保内存状态的一致性。
	return 0;
}
```


### 1.2.10 unsigned long put_page(unsigned long page,unsigned long address)

#### 1.2.10.1 作用

>将一个**物理页**(page)放置在**内存中指定的线性地址**(address)处，并返回**该页的物理地址**。
如果内存不足（无论是访问**页表**还是**页面**时），则返回 0。

#### 1.2.10.2 代码实现
```c
/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page,unsigned long address)
//page表示要放置的页面的物理地址，address表示期望放置页面的线性地址。
{
	unsigned long tmp, *page_table;
    //tmp，用于临时存储获取到的空闲页面地址；
    //page_table，用于指向页表。

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
    //检查物理地址的有效性。
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
    //检查mem_map数组中对应页面的引用计数是否不为 1.
	page_table = (unsigned long *) ((address>>20) & 0xffc);
    //通过将线性地址address右移 20 位并与0xffc（低 2 位为 0）进行与操作，得到页目录项的地址，并将其转换为指针类型赋给page_table。这一步是为了定位与目标线性地址对应的页表。
	if ((*page_table)&1)//页表是否存在。
		page_table = (unsigned long *) (0xfffff000 & *page_table);//如果页表已存在，从页目录项中提取页表的物理地址（通过与0xfffff000进行与操作），并将page_table指向该页表。
	else {
		if (!(tmp=get_free_page()))//调用get_free_page函数获取一个空闲页面，将返回的地址存储在tmp中。如果获取失败（tmp为 0）。
			return 0;
		*page_table = tmp|7;//如果获取到空闲页面，将其地址存储在页目录项中，并设置一些标志位（| 7可能设置了页表存在、可读可写等标志，具体取决于系统定义）。
		page_table = (unsigned long *) tmp;//将page_table指向新分配的页表。
	}
	page_table[(address>>12) & 0x3ff/*10位*/] = page | 7;//计算目标线性地址在页表中的索引（(address>>12) & 0x3ff），并在页表的对应项中存储要放置的页面的物理地址page，同时设置一些标志位（| 7）。
    /* no need for invalidate */
	return page;//返回放置的页面的物理地址，表示操作成功。
}
```
### 1.2.11 void un_wp_page(unsigned long * table_entry)

#### 1.2.11.1 作用

归根到底是为了改动这个页表项目table_entry

>1.取消页表项的**写保护位**，将页面设置为**可读写**。

>2.处理写时复制情况：如果页面**只被一个进程使用**，只需**设置写标志**，否则**需要复制页面**。

#### 1.2.11.2 代码实现

```c
void un_wp_page(unsigned long * table_entry/*table_entry,该指针指向一个页表项*/)
{
	unsigned long old_page,new_page;//old_page和new_page，分别用于存储旧页面的物理地址和新分配页面的物理地址。

	old_page = 0xfffff000 & *table_entry;//从传入的页表项*table_entry中提取出旧页面的物理地址，并存储在old_page变量中。
    //物理地址的高20位，用于提取旧页面的物理地址。

    //这里就可以回顾一下之前，我们的物理地址是怎么得来的了：

    //得到线性地址后，线性地址被分成3份，格式如下：
    /*
        +---------+------+------+
        |高10位   |中10位|低12位|
        +---------+------+------+
        我们根据这3份地址，就可以找到页表项，页表项的具体格式：
                页目录项/页表项格式：
        31                    12 11    9 8 7 6 5 4 3 2 1 0
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
        | 页表物理地址高20位    | AVL   |0|0|0|A|0|0|U|W|P|
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
        这里，我们就可以得到具体的页面的物理地址的高20位了。再和线性地址的低12位拼接起来，就可以得到具体的页面的物理地址了。
        所以说，页目录项、页表格式是**距离物理地址最后的格式**了，因为从他们这里找到页表的物理地址后，**再加上偏移地址**(线性地址的低12位)，就得到了**物理地址**。
    */
    //检查旧的页面是否可以直接处理。
    //检查旧页面的物理地址是否在低内存范围之上（old_page >= LOW_MEM），并且该页面在mem_map数组中的引用计数是否为 1(只被一个进程使用)
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
        //满足条件，就直接设置写标志位。
		*table_entry |= 2;
		invalidate();//使缓存（如处理器的页表缓存 TLB）无效，确保系统能够正确反映页表项的变化。
		return;
	}
    //下面就是被多个进程只用了，还需要复制页面。

    //调用get_free_page函数尝试获取一个空闲页面，并将返回的物理地址存储在new_page变量中。
	if (!(new_page=get_free_page()))
		oom();//oom函数在前面也讲了，就是out of Memory。
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;//减少mem_map数组中对应旧页面的引用计数，表示旧页面的使用者减少一个。
	*table_entry = new_page | 7;//设置相关内容，111开放。
	invalidate();
	copy_page(old_page,new_page);//将旧页面的内容复制到新页面。
}	
```


### 1.2.12 void do_wp_page(unsigned long error_code,unsigned long address)

#### 1.2.12.1 作用
用于**处理页面缺失异常**，在给定发生页面错误的**线性地址**和**错误码**后，通过**检查地址范围**、**尝试共享页面**、**分配并填充新页面**等操作，`将合适的页面映射到指定地址`，若过程中出现**内存不足**等问题则进行相应处理。

#### 1.2.12.2 代码实现
```c
void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];/*用于存储块设备映射信息*/
	unsigned long tmp;//临时存储计算过程中的地址或偏移量。
	unsigned long page;//用于存储获取到的空闲页面的物理地址。
	int block,i;//block可能用于表示文件系统中的块编号

	address &= 0xfffff000;//得到物理基地址，高20位。同时也将address对齐到页面边界，即address是2^12的倍数，即是4kb的倍数，完成了页面对齐。
	tmp = address - current->start_code;//计算发生页面错误的地址address相对于当前进程代码起始地址current->start_code的偏移量，并存储在tmp中。
	if (!current->executable || tmp >= current->end_data) {//检查当前进程是否有可执行文件（current->executable是否为NULL），或者偏移量tmp是否超出了当前进程数据段的结束地址current->end_data。
		get_empty_page(address);//调用get_empty_page函数获取一个空页面，并将其映射到address处。
		return;//直接返回，在这种情况下，已经处理了页面缺失问题。
	}
	if (share_page(tmp))//尝试共享页面，该函数可能根据偏移量tmp检查是否可以与其他进程共享页面。
		return;//可以共享，也返回了，处理了问题。
	if (!(page = get_free_page()))//获取一个空闲页面，并将返回的物理地址存储在page变量中。
		oom();//处理oom

    /* remember that 1 block is used for header */
    /*这里就涉及了文件系统的处理，必须理清才能完全明白
    
    
    */
	block = 1 + tmp/BLOCK_SIZE;//计算与当前页面相关的文件系统块编号。这里BLOCK_SIZE是文件系统块的大小，tmp是页面相对于进程代码起始地址的偏移量
	for (i=0 ; i<4 ; block++,i++)//4次循环，也就是函数刚开始的4个数组。
		nr[i] = bmap(current->executable,block);//调用bmap函数获取当前进程可执行文件中对应块编号block的块设备映射，并将结果存储在nr数组中。
    /*有没有想过，为啥是4？
                
                页表项格式：
        31                    12 11    9 8 7 6 5 4 3 2 1 0
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
        | 页面物理地址高20位     | AVL   |0|0|0|A|0|W|U|P|
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
    */

	bread_page(page,current->executable->i_dev,nr);//调用bread_page函数从块设备中读取页面数据到新分配的页面page中。current->executable->i_dev表示当前进程可执行文件所在的设备，nr数组包含了要读取的块编号信息。
	i = tmp + 4096 - current->end_data;//计算需要填充为 0 的字节数。tmp是页面相对于进程代码起始地址的偏移量，加上 4096（页面大小）再减去进程数据段结束地址current->end_data，得到页面中超出进程数据段部分的字节数。
	tmp = page + 4096;//将tmp设置为页面末尾的下一个地址。
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;//将tmp指向的字节设置为 0，即将页面超出进程数据段部分填充为 0。
	}
	if (put_page(page,address))//调用put_page函数将获取到的页面page映射到发生页面错误的地址address处。
		return;
	free_page(page);//如果页面映射失败，调用free_page函数释放之前分配的页面。
	oom();
}
```

这里要讲解清楚bread_page函数：
我们要知道的是，任何和外部设备的读写，都是通过缓冲区来实现的，所以bread_page函数的作用就是**将四个缓冲区的数据读取到所需地址的内存中**。
同时，它同时读取，能加快速率。
3
关于缓冲区的内容，linus将其归在fs文件夹下，说明他和文件系统密切相关。我们可以查看：
[buffer.c的实现](../src/fs/buffer.c)

```c
/*
bread_page函数将四个缓冲区的数据读取到所需地址的内存中。
它之所以是一个独立的函数，是因为**同时读取**这四个缓冲区的数据能够提高速度，
而无需等待一个缓冲区读完再读下一个，依此类推。
*/

void bread_page(unsigned long address,int dev,int b[4])
{
	struct buffer_head * bh[4];
	int i;

	for (i=0 ; i<4 ; i++)
		if (b[i]) {
			if ((bh[i] = getblk(dev,b[i])))
				if (!bh[i]->b_uptodate)
					ll_rw_block(READ,bh[i]);
		} else
			bh[i] = NULL;
	for (i=0 ; i<4 ; i++,address += BLOCK_SIZE)
		if (bh[i]) {
			wait_on_buffer(bh[i]);
			if (bh[i]->b_uptodate)
				COPYBLK((unsigned long) bh[i]->b_data,address);
			brelse(bh[i]);
		}
}
```
### 1.2.13 void write_verify(unsigned long address)
#### 1.2.13.1 函数作用
检查给定线性地址对应的页面**是否可写**。如果页面存在但不可写，函数会采取措施（如**调用un_wp_page函数**）**使其可写**。

#### 1.2.13.2 函数实现
```c
void write_verify(unsigned long address/*线性地址*/)
{
	unsigned long page;//存储与线性地址相关的页表或页面的物理地址信息。

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
    //(address>>20) & 0xffc：右移 20 位是为了提取页目录索引部分，与0xffc与操作确保地址 4 字节对齐。

	page &= 0xfffff000;//从页目录项的值中提取页表的物理地址
	page += ((address>>10) & 0xffc);//(address>>10) & 0xffc：右移 10 位是为了提取页偏移地址部分，与0xffc与操作确保地址 4 字节对齐。然后将其与前面提取的页表物理地址相加，得到要操作的页表项的实际物理地址。注意，这里仍然得到的是页表项的物理地址，而不是页面的物理地址。要得到实际的物理内存地址，需要从页表项内容中提取页面物理地址高 20 位，再与线性地址的低 12 位（页内偏移）组合。
    
	if ((3 & *(unsigned long *) page) == 1) /*即页面里面的低2位为11，也即 non-writeable, present */
    /*
               
                页表项格式：
        31                    12 11    9 8 7 6 5 4 3 2 1 0
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
        | 页面物理地址高20位     | AVL   |0|0|0|A|0|W|U|P|
        +----------------------+--------+-+-+-+-+-+-+-+-+-+
    */
		un_wp_page((unsigned long *) page);//那么就调用un_wp_page函数将页面设置为可写。
	return;
}
```
### 1.2.14 void get_empty_page(unsigned long address)

### 1.2.15 static int try_to_share(unsigned long address, struct task_struct * p)

### 1.2.16 static int share_page(unsigned long address)

### 1.2.17 void do_no_page(unsigned long error_code,unsigned long address)

### 1.2.18 void mem_init(long start_mem, long end_mem)

### 1.2.19 void calc_mem(void)



# 2.memory.c的源码分析

## 2.1 memory.c的全部实现
老方法，我们先给出全貌，再逐行代码分析：

```c
/*
 *  linux/mm/memory.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * demand-loading started 01.12.91 - seems it is high on the list of
 * things wanted, and it should be easy to implement. - Linus
 */

/*
 * Ok, demand-loading was easy, shared pages a little bit tricker. Shared
 * pages started 02.12.91, seems to work. - Linus.
 *
 * Tested sharing by executing about 30 /bin/sh: under the old kernel it
 * would have taken more than the 6M I have free, but it worked well as
 * far as I could see.
 *
 * Also corrected some "invalidate()"s - I wasn't doing enough of them.
 */

#include <signal.h>

#include <asm/system.h>

#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>

void do_exit(long code);

static inline void oom(void)
{
	printk("out of memory\n\r");
	do_exit(SIGSEGV);
}

#define invalidate() \
__asm__("movl %%eax,%%cr3"::"a" (0))

/* these are not to be changed without changing head.s etc */
#define LOW_MEM 0x100000
#define PAGING_MEMORY (15*1024*1024)
#define PAGING_PAGES (PAGING_MEMORY>>12)
#define MAP_NR(addr) (((addr)-LOW_MEM)>>12)
#define USED 100

#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
current->start_code + current->end_code)

static long HIGH_MEMORY = 0;

#define copy_page(from,to) \
__asm__("cld ; rep ; movsl"::"S" (from),"D" (to),"c" (1024))

static unsigned char mem_map [ PAGING_PAGES ] = {0,};

/*
 * Get physical address of first (actually last :-) free page, and mark it
 * used. If no free pages left, return 0.
 */
unsigned long get_free_page(void)
{
register unsigned long __res asm("ax");

__asm__("std ; repne ; scasb\n\t"
	"jne 1f\n\t"
	"movb $1,1(%%edi)\n\t"
	"sall $12,%%ecx\n\t"
	"addl %2,%%ecx\n\t"
	"movl %%ecx,%%edx\n\t"
	"movl $1024,%%ecx\n\t"
	"leal 4092(%%edx),%%edi\n\t"
	"rep ; stosl\n\t"
	" movl %%edx,%%eax\n"
	"1: cld"
	:"=a" (__res)
	:"0" (0),"i" (LOW_MEM),"c" (PAGING_PAGES),
	"D" (mem_map+PAGING_PAGES-1)
	);
return __res;
}

/*
 * Free a page of memory at physical address 'addr'. Used by
 * 'free_page_tables()'
 */
void free_page(unsigned long addr)
{
	if (addr < LOW_MEM) return;
	if (addr >= HIGH_MEMORY)
		panic("trying to free nonexistent page");
	addr -= LOW_MEM;
	addr >>= 12;
	if (mem_map[addr]--) return;
	mem_map[addr]=0;
	panic("trying to free free page");
}

/*
 * This function frees a continuos block of page tables, as needed
 * by 'exit()'. As does copy_page_tables(), this handles only 4Mb blocks.
 */
int free_page_tables(unsigned long from,unsigned long size)
{
	unsigned long *pg_table;
	unsigned long * dir, nr;

	if (from & 0x3fffff)
		panic("free_page_tables called with wrong alignment");
	if (!from)
		panic("Trying to free up swapper memory space");
	size = (size + 0x3fffff) >> 22;
	dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	for ( ; size-->0 ; dir++) {
		if (!(1 & *dir))
			continue;
		pg_table = (unsigned long *) (0xfffff000 & *dir);
		for (nr=0 ; nr<1024 ; nr++) {
			if (1 & *pg_table)
				free_page(0xfffff000 & *pg_table);
			*pg_table = 0;
			pg_table++;
		}
		free_page(0xfffff000 & *dir);
		*dir = 0;
	}
	invalidate();
	return 0;
}

/*
 *  Well, here is one of the most complicated functions in mm. It
 * copies a range of linerar addresses by copying only the pages.
 * Let's hope this is bug-free, 'cause this one I don't want to debug :-)
 *
 * Note! We don't copy just any chunks of memory - addresses have to
 * be divisible by 4Mb (one page-directory entry), as this makes the
 * function easier. It's used only by fork anyway.
 *
 * NOTE 2!! When from==0 we are copying kernel space for the first
 * fork(). Then we DONT want to copy a full page-directory entry, as
 * that would lead to some serious memory waste - we just copy the
 * first 160 pages - 640kB. Even that is more than we need, but it
 * doesn't take any more memory - we don't copy-on-write in the low
 * 1 Mb-range, so the pages can be shared with the kernel. Thus the
 * special case for nr=xxxx.
 */
int copy_page_tables(unsigned long from,unsigned long to,long size)
{
	unsigned long * from_page_table;
	unsigned long * to_page_table;
	unsigned long this_page;
	unsigned long * from_dir, * to_dir;
	unsigned long nr;

	if ((from&0x3fffff) || (to&0x3fffff))
		panic("copy_page_tables called with wrong alignment");
	from_dir = (unsigned long *) ((from>>20) & 0xffc); /* _pg_dir = 0 */
	to_dir = (unsigned long *) ((to>>20) & 0xffc);
	size = ((unsigned) (size+0x3fffff)) >> 22;
	for( ; size-->0 ; from_dir++,to_dir++) {
		if (1 & *to_dir)
			panic("copy_page_tables: already exist");
		if (!(1 & *from_dir))
			continue;
		from_page_table = (unsigned long *) (0xfffff000 & *from_dir);
		if (!(to_page_table = (unsigned long *) get_free_page()))
			return -1;	/* Out of memory, see freeing */
		*to_dir = ((unsigned long) to_page_table) | 7;
		nr = (from==0)?0xA0:1024;
		for ( ; nr-- > 0 ; from_page_table++,to_page_table++) {
			this_page = *from_page_table;
			if (!(1 & this_page))
				continue;
			this_page &= ~2;
			*to_page_table = this_page;
			if (this_page > LOW_MEM) {
				*from_page_table = this_page;
				this_page -= LOW_MEM;
				this_page >>= 12;
				mem_map[this_page]++;
			}
		}
	}
	invalidate();
	return 0;
}

/*
 * This function puts a page in memory at the wanted address.
 * It returns the physical address of the page gotten, 0 if
 * out of memory (either when trying to access page-table or
 * page.)
 */
unsigned long put_page(unsigned long page,unsigned long address)
{
	unsigned long tmp, *page_table;

/* NOTE !!! This uses the fact that _pg_dir=0 */

	if (page < LOW_MEM || page >= HIGH_MEMORY)
		printk("Trying to put page %p at %p\n",page,address);
	if (mem_map[(page-LOW_MEM)>>12] != 1)
		printk("mem_map disagrees with %p at %p\n",page,address);
	page_table = (unsigned long *) ((address>>20) & 0xffc);
	if ((*page_table)&1)
		page_table = (unsigned long *) (0xfffff000 & *page_table);
	else {
		if (!(tmp=get_free_page()))
			return 0;
		*page_table = tmp|7;
		page_table = (unsigned long *) tmp;
	}
	page_table[(address>>12) & 0x3ff] = page | 7;
/* no need for invalidate */
	return page;
}

void un_wp_page(unsigned long * table_entry)
{
	unsigned long old_page,new_page;

	old_page = 0xfffff000 & *table_entry;
	if (old_page >= LOW_MEM && mem_map[MAP_NR(old_page)]==1) {
		*table_entry |= 2;
		invalidate();
		return;
	}
	if (!(new_page=get_free_page()))
		oom();
	if (old_page >= LOW_MEM)
		mem_map[MAP_NR(old_page)]--;
	*table_entry = new_page | 7;
	invalidate();
	copy_page(old_page,new_page);
}	

/*
 * This routine handles present pages, when users try to write
 * to a shared page. It is done by copying the page to a new address
 * and decrementing the shared-page counter for the old page.
 *
 * If it's in code space we exit with a segment error.
 */
void do_wp_page(unsigned long error_code,unsigned long address)
{
#if 0
/* we cannot do this yet: the estdio library writes to code space */
/* stupid, stupid. I really want the libc.a from GNU */
	if (CODE_SPACE(address))
		do_exit(SIGSEGV);
#endif
	un_wp_page((unsigned long *)
		(((address>>10) & 0xffc) + (0xfffff000 &
		*((unsigned long *) ((address>>20) &0xffc)))));

}

void write_verify(unsigned long address)
{
	unsigned long page;

	if (!( (page = *((unsigned long *) ((address>>20) & 0xffc)) )&1))
		return;
	page &= 0xfffff000;
	page += ((address>>10) & 0xffc);
	if ((3 & *(unsigned long *) page) == 1)  /* non-writeable, present */
		un_wp_page((unsigned long *) page);
	return;
}

void get_empty_page(unsigned long address)
{
	unsigned long tmp;

	if (!(tmp=get_free_page()) || !put_page(tmp,address)) {
		free_page(tmp);		/* 0 is ok - ignored */
		oom();
	}
}

/*
 * try_to_share() checks the page at address "address" in the task "p",
 * to see if it exists, and if it is clean. If so, share it with the current
 * task.
 *
 * NOTE! This assumes we have checked that p != current, and that they
 * share the same executable.
 */
static int try_to_share(unsigned long address, struct task_struct * p)
{
	unsigned long from;
	unsigned long to;
	unsigned long from_page;
	unsigned long to_page;
	unsigned long phys_addr;

	from_page = to_page = ((address>>20) & 0xffc);
	from_page += ((p->start_code>>20) & 0xffc);
	to_page += ((current->start_code>>20) & 0xffc);
/* is there a page-directory at from? */
	from = *(unsigned long *) from_page;
	if (!(from & 1))
		return 0;
	from &= 0xfffff000;
	from_page = from + ((address>>10) & 0xffc);
	phys_addr = *(unsigned long *) from_page;
/* is the page clean and present? */
	if ((phys_addr & 0x41) != 0x01)
		return 0;
	phys_addr &= 0xfffff000;
	if (phys_addr >= HIGH_MEMORY || phys_addr < LOW_MEM)
		return 0;
	to = *(unsigned long *) to_page;
	if (!(to & 1)) {
		if ((to = get_free_page()))
			*(unsigned long *) to_page = to | 7;
		else
			oom();
	}
	to &= 0xfffff000;
	to_page = to + ((address>>10) & 0xffc);
	if (1 & *(unsigned long *) to_page)
		panic("try_to_share: to_page already exists");
/* share them: write-protect */
	*(unsigned long *) from_page &= ~2;
	*(unsigned long *) to_page = *(unsigned long *) from_page;
	invalidate();
	phys_addr -= LOW_MEM;
	phys_addr >>= 12;
	mem_map[phys_addr]++;
	return 1;
}

/*
 * share_page() tries to find a process that could share a page with
 * the current one. Address is the address of the wanted page relative
 * to the current data space.
 *
 * We first check if it is at all feasible by checking executable->i_count.
 * It should be >1 if there are other tasks sharing this inode.
 */
static int share_page(unsigned long address)
{
	struct task_struct ** p;

	if (!current->executable)
		return 0;
	if (current->executable->i_count < 2)
		return 0;
	for (p = &LAST_TASK ; p > &FIRST_TASK ; --p) {
		if (!*p)
			continue;
		if (current == *p)
			continue;
		if ((*p)->executable != current->executable)
			continue;
		if (try_to_share(address,*p))
			return 1;
	}
	return 0;
}

void do_no_page(unsigned long error_code,unsigned long address)
{
	int nr[4];
	unsigned long tmp;
	unsigned long page;
	int block,i;

	address &= 0xfffff000;
	tmp = address - current->start_code;
	if (!current->executable || tmp >= current->end_data) {
		get_empty_page(address);
		return;
	}
	if (share_page(tmp))
		return;
	if (!(page = get_free_page()))
		oom();
/* remember that 1 block is used for header */
	block = 1 + tmp/BLOCK_SIZE;
	for (i=0 ; i<4 ; block++,i++)
		nr[i] = bmap(current->executable,block);
	bread_page(page,current->executable->i_dev,nr);
	i = tmp + 4096 - current->end_data;
	tmp = page + 4096;
	while (i-- > 0) {
		tmp--;
		*(char *)tmp = 0;
	}
	if (put_page(page,address))
		return;
	free_page(page);
	oom();
}

void mem_init(long start_mem, long end_mem)
{
	int i;

	HIGH_MEMORY = end_mem;
	for (i=0 ; i<PAGING_PAGES ; i++)
		mem_map[i] = USED;
	i = MAP_NR(start_mem);
	end_mem -= start_mem;
	end_mem >>= 12;
	while (end_mem-->0)
		mem_map[i++]=0;
}

void calc_mem(void)
{
	int i,j,k,free=0;
	long * pg_tbl;

	for(i=0 ; i<PAGING_PAGES ; i++)
		if (!mem_map[i]) free++;
	printk("%d pages free (of %d)\n\r",free,PAGING_PAGES);
	for(i=2 ; i<1024 ; i++) {
		if (1&pg_dir[i]) {
			pg_tbl=(long *) (0xfffff000 & pg_dir[i]);
			for(j=k=0 ; j<1024 ; j++)
				if (pg_tbl[j]&1)
					k++;
			printk("Pg-dir[%d] uses %d pages\n",i,k);
		}
	}
}
```
