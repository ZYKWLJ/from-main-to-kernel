# 1.前情回顾

上文说到，操作系统要运行，就必须加载到内存中，经过bootsect.S加载，操作系统成功运行在内存中了；但是一些重要的设置还没有完成，并且CPU依然工作在实模式下，所以我们进行了setup.S阶段，完成元数据保存、system段的位移、进入保护模式前的必要操作等工作，如设置GDT表等。

好了，那么接下来，我们CPU要跑到哪里去执行呢？

我们通过代码来看：

```s
ljmp	$sel_cs0, $0 # sel_cs0=0x0008
```
## 1.1setup后跳转到哪里？

### 1.1.1段基地址：

将sel_cs0解释为1-00-0，可以看到，我们找到GDT的索引为1的描述符表项：
```s
    # (2) 代码段描述符（索引 1，选择子 0x0008）
	.word	0x07FF		# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		# base address=0
	.word	0x9A00		# code read/exec
	.word	0x00C0		# granularity=4096, 386
```

我们按照描述符组成挨个对比，得到8字节如下：

>FF-70-00-00-00-A9-0C-00

16~31位置：00-00
32~39位置：00
48~63位置：00

![段描述符](../img/setup/段描述符.png)

所以基地址为0x0000.

### 1.1.2.偏移地址
显然，`ljmp	$sel_cs0, $0`中,偏移地址为0.

>所以，经过setup.S后，我们CPU转向0x0000执行 代码，而那里刚好是setup.S移过去的System(head模块)！一切都串起来了！


# 2.head.s功能及原理

## 2.1 head.s的两点功能及解释

|head.S的功能|解释|
|-|-|
|1. 重新设置idt、gdt，并设置256哑中端|现在进入保护模式了，寻址方式完全变化了，段寄存器里面存的是GDT的索引了。获取段基地址的方式完成不同了，所以必须设置idt、gdt，供寻址使用。同时“重新”一词，是因为setup.S会被覆盖，所以是对其**被覆盖前的数据备份**。|
|2. 开启分页机制|显然，要开启分页模式，分页模式是在保护模式上进一步系统高效运行的增强。|

# 3.head.s源码解析
## 3.1 全部源码
```s
/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */

.text
.globl idt,gdt,pg_dir,tmp_floppy_area
pg_dir:
.globl startup_32
startup_32:
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	call setup_idt
	call setup_gdt
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds		# after changing gdt. CS was already
	mov %ax,%es		# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	lss stack_start,%esp
	xorl %eax,%eax
1:	incl %eax		# check that A20 really IS enabled
	movl %eax,0x000000	# loop forever if it isn't
	cmpl %eax,0x100000
	je 1b

/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	# Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		# set MP
	movl %eax,%cr0
	call check_x87
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */

check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* no coprocessor: have to set bits */
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0
	ret
.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */

setup_idt:
	lea ignore_int,%edx
	movl $0x00080000,%eax
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	lidt idt_descr
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */

setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */

.org 0x1000
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000

/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isnt
 * on a 64kB border.
 */

tmp_floppy_area:
	.fill 1024,1,0

after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to.
	pushl $main
	jmp setup_paging
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */

int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. Ive
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldnt be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * Ive tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * wont guarantee thats all :-( )
 */

.align 2
setup_paging:
	movl $1024*5,%ecx		/* 5 pages - pg_dir+4 page tables */
	xorl %eax,%eax
	xorl %edi,%edi			/* pg_dir is at 0x000 */
	cld;rep;stosl
	movl $pg0+7,pg_dir		/* set present bit/user r/w */
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */
	movl $pg3+4092,%edi
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax
	jge 1b
	cld
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/* cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */

.align 2
.word 0
idt_descr:
	.word 256*8-1		# idt contains 256 entries
	.long idt
.align 2
.word 0
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)

	.align 8
idt:	.fill 256,8,0		# idt is uninitialized

gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */
	.quad 0x00c0920000000fff	/* 16Mb */
	.quad 0x0000000000000000	/* TEMPORARY - dont use */
	.fill 252,8,0			/* space for LDTs and TSSs etc */
```

## 3.2 head.S 源码逐一讲解

### 3.2.1 从16bit->32bit 的核心说明

该文件是 32 位启动代码，运行在**物理地址 0x00000000**（实模式切换保护模式后的入口）；

此地址也是**后续页目录（pg_dir）**的存放地址，因此启动代码执行完成后**会被页目录覆盖，无需保留。**

### 3.2.2 定义段和全局符号
```s
    # .text：定义代码段（只读、可执行），汇编器将后续代码放入代码段；
    .text
    
    # .globl xxx：声明全局符号，让链接器可见，供内核其他模块（如 C 代码）引用；
    .globl idt, gdt, pg_dir, tmp_floppy_area
    
    # pg_dir:：页目录起始地址标记（物理 0x00000000），后续分页机制的核心结构；
    pg_dir:

    # startup_32:：32 位保护模式启动入口
    .globl startup_32
```

### 3.2.3 段寄存器、esp再次初始化
如下，就是将段寄存器 ds, es, fs, gs **都设置为 0x10**，
并将栈指针 esp 设置为 **stack_start**。
```s
startup_32:
    # 设置段寄存器 ds, es, fs, gs 为 0x10
    movl $0x10,%eax
    mov %ax,%ds
    mov %ax,%es
    mov %ax,%fs
    mov %gs
    # 设置堆栈指针 esp 为 stack_start
    lss stack_start,%esp
```
注意，段寄存器为啥是0x10? 堆栈指针又为啥是stack_start? 别急，让我慢慢为您讲述！

#### 3.2.3.1 段寄存器为啥是0x10?

我们不要忘记，段寄存器里面存的是段选择子，段选择子由3段，段选择子是16位的结构，前13位为**段描述符表索引**，

具体组成及功能如下：

![段选择子](../img/setup/段选择子.png)

|字段|位数|作用|
|-|-|-|
|索引|3-15，13位|指向GDT或LDT中的一个段描述符|
|TI，即Table Indicator（表指示位 / 表指示器）|2，1位|表指示位，0表示GDT，1表示LDT|
|RPL，Requestor Privilege Level（请求特权级）|0-1，2位|请求特权级，用于权限检查|



因为现在是GDT，所以TI为0；又因为内核处于ring0，所以RPL为0。而前面我们设置gdt的第1、第2个表项了，现在我们要设置gdt的第3个表项，所以gdt的索引为2，即0b10，则：

段选择子的值应该为：0b10-0-00，换成十六进制为0x10。

那描述符的具体值为多少呢？

见setup.s里面的设置：
```s
gdt:
    # 索引为0的描述符
	.word	0,0,0,0		# dummy
    
    # 索引为1的描述符
	.word	0x07FF		# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		# base address=0
	.word	0x9A00		# code read/exec
	.word	0x00C0		# granularity=4096, 386

    # 索引为2的描述符
	.word	0x07FF		# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		# base address=0
	.word	0x9200		# data read/write
	.word	0x00C0		# granularity=4096, 386
```

所以，为什么要设置为ox10？这就是段选择子的结构使然，这里需要段选择子0x10指向的描述符是索引为2的描述符，结合结构就是0b10-0-00，即0x10。

>实际上，这也是我们的数据段！


#### 3.2.3.2 堆栈指针 esp 为啥是 stack_start?
首先明确，esp是用来指向栈顶的指针，那说明，现在我们要将栈顶设置为stack_start。
顾名生义，这就是stack的start。

我们有一个直觉，就是栈顶一定要高高的，这样防止与下面的内容相撞。这个思想在bootsect中，内存的初始规划中，设置为0xff00就已经有体现了。

那stack_start定义在哪里？

stack_start定义在[stack_start定义](../src/kernel/sched.c)

其中，相关的PAGE_SIZE定义在[PAGE_SIZE定义](../src/include/linux/mm.h)

```c
//  [include/linux/mm.h]
#define PAGE_SIZE 4096 

//  kernel/sched.c
long user_stack [ PAGE_SIZE>>2 ] ;// 1024项6字节元素的数组。

//
struct {
	long * a;//4字节
	short b; //2字节
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
```
所以，user_stack 是一个包含`1024个长整型（4字节）元素的数组`，总大小正好是 1024 * 4 = 4096 字节，即 `一个物理内存页。`

那显然，stack_start就是一个结构体地址标签。


#### 3.2.3.3 从头到尾讲解初次设置内核栈

我们从头到尾讲解一下为啥这么设置。

##### 3.2.3.3.1 核心目标

在 head.S 中执行 `lss stack_start, %esp` 这条指令的核心目标是：

为即将进入的保护模式**初始化内核堆栈段和指针（SS:ESP）。**

它设置了**第一个真正意义上的内核堆栈**，供初始化代码和后续的**第一个进程（进程0）的内核态使用。**

##### 3.2.3.3.2 背景-从实模式到保护模式

在 head.S 执行这条指令之前，系统已经完成了以下关键步骤：

- 1. 实模式阶段：

由 bootsect.S 和 setup.S 完成，它们将内核代码（system模块）加载到内存0x0000_0000处。

- 2. 进入保护模式：

setup.S 加载了GDT（全局描述符表），并设置了CR0寄存器的保护模式位。

- 3. 跳转到32位代码：

通过 jmpi 0, 8 跳转到 head.S（在物理地址0x0000_0000处），此时段寄存器CS的值为8（指向GDT中第一个代码段描述符）。但其他段寄存器（如SS）还是实模式的值，没有正确指向保护模式下的数据段。

- 4. 重新设置段寄存器：

head.S 需要重新加载所有**数据段寄存器**（DS, ES, FS, GS, SS），使其**指向正确的保护模式描述符**（**内核数据段**）。

lss stack_start, %esp 就是**完成设置SS和ESP**这一关键步骤的指令。

##### 3.2.3.3.3 lss指令-原子性执行 偏移量+段选择子

1. 指令：lss (Load Segment and Stack Pointer)

2. 格式：**lss m32, reg32**

3. 功能：从一个**内存地址 m32 处加载一个48位的数据**（**32位偏移量 + 16位段选择子**），并将偏移量加载到指定的**32位寄存器 reg32（这里是 %esp）**，将**段选择子加载到段寄存器 ss**。

4. 作用：**一条指令，同时设置栈指针（ESP）和栈段（SS）**，确保它们原子性地同步更新，这在初始化阶段非常重要。

##### 3.2.3.3.4 讲解含义

```c
struct {
	long * a;//4字节
	short b; //2字节
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
```
这里， `stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };` ,

前者恰好是user_stack的最后一个元素(1024个)，指向了 user_stack 这个4KB内存区域的最高地址（栈顶）。

后者0x10,解析下来为索引2对应的描述符是内核数据段描述符。

#####  3.2.3.3.5 最终功能
所以，lss 指令会从 stack_start 这个标签处**读取连续的6个字节**：**前4字节作为ESP**，**后2字节作为SS**。

由此完成内存堆栈的设置！

##### 3.2.3.3.6 为啥需要这么设置？

好了，我们上面都是顺藤摸瓜解释user_stack的实现，那么我们有没有想过，为什么需要 user_stack 和这种设置呢？

我们给他讲解透：

###### 3.2.3.3.6.1 内核需要一个栈：

在保护模式下执行C语言函数（比如马上要调用的 main()），**必须有正确的栈来存放返回地址、局部变量、函数参数等。**

###### 3.2.3.3.6.2 为进程0做准备：

在Linux 0.11中，user_stack 这个数组**不仅仅是临时的启动栈**。它被设计为第一个任务（任务0，即空闲任务）的内核态堆栈。

###### 3.2.3.3.6.3 每个任务有两个栈：

**用户态栈和内核态栈**。当任务在用户态运行并**发生系统调用**或**中断**时，CPU会切换到**该任务的内核态栈**。

任务0比较特殊，它**没有用户态代码**，它**永远运行在内核态**（init/main.c 中的 **for(;;) pause();**）。所以它的内核态栈也**是它唯一的栈。**

task_struct（进程描述符）中的 **tss.esp0** 字段（**内核栈指针**）在初始化时就被**设置为指向这个 user_stack 的顶部**。lss 指令是第一次设置，后续任务切换会通过TSS来恢复这个栈。

###### 3.2.3.3.6.4 大小合适：

4KB是一个标准内存页的大小，也是内核为**单个任务的内核栈分配的经典尺寸**（即使在现代Linux中，内核栈通常也是8KB或16KB，但在当时4KB是合理且节省的）。