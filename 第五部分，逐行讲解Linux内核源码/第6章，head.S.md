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
    # 后者从这里开始覆盖
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

https://www.doubao.com/thread/wd855247119dc5695

### 3.2.4 设置gdt、idt表
```s
# 调用中断描述符表（IDT）初始化函数，构建 256 个中断门；
call setup_idt
# 调用全局描述符表（GDT）重新加载函数，更新 GDT 并刷新段描述符缓存；
call setup_gdt
```

我们看看具体实现：
#### 3.2.4.1 setup_idt

核心功能：构建` 256 个中断门描述符`（**每个 8 字节**），统一指向默认**中断处理函数ignore_int**，**最后加载 IDT**；
##### 3.2.4.1.1 setup_idt
```s
setup_idt:
	lea ignore_int,%edx # 取默认中断处理函数ignore_int的偏移地址（32 位）到 EDX；
	movl $0x00080000,%eax   # EAX 高 16 位置 0x0008（内核代码段选择子，GDT 第 1 个非空描述符）；
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present(存在) */
                        # DX 设置中断门属性（高 16 位），0x8E00 解析：
                        # 0x8000：P 位 = 1（存在位，Present），描述符有效；
                        # 0x0E00：类型 = 14（中断门），DPL=0（特权级 0，仅内核可访问）；
	lea idt,%edi        # EDI 指向 IDT 起始地址，作为写入指针；
	mov $256,%ecx       # ECX 设为循环计数器（256 个中断门）；

rp_sidt:
	movl %eax,(%edi)    # 写入中断门低 32 位（选择子 + 偏移低 16 位）；
	movl %edx,4(%edi)   # 写入中断门高 32 位（属性 + 偏移高 16 位）；
	addl $8,%edi        # EDI 后移 8 字节，指向下一个中断门；
	dec %ecx            # 计数器减 1
	jne rp_sidt         # 未到 0 则继续循环；
	lidt idt_descr      # 加载 IDT 描述符（包含 IDT 大小和基址）到 CPU 的 IDTR 寄存器，使 IDT 生效；
	ret  # 函数返回
```
##### 3.2.4.1.2 idt_descr 描述符表寄存器（/IDTR）初始化数据
对应的idt_descr:
我们知道，idt_descr用来加载idt的地址，那么就必须知道idt在哪里加载，加载多少！

```s
.align 2 # 2^2=4字节对齐               
.word 0  # 2字节，用于明确强制对齐。2+6(下面的idt_descr)=8字节对齐
idt_descr:              # 2字节，IDT 描述符，供lidt指令加载
	.word 256*8-1		# 4字节，描述表项的总大小，idt contains 256 entries
	.long idt           # 2字节，IDT 基址，指向 IDT 数组起始地址
```


对应的idt:
##### 3.2.4.1.3 idt
对应的idt，实实在在的中断门描述符表，这个表有256个项。
```s
.align 8 # 2^8=256字节对齐
idt:	
.fill 256,8,0		# idt is uninitialized,
                    # 填充 256 个 8 字节项，值均为 0—— 实际初始化在setup_idt中完成，此处仅预留空间。
```

#### 3.2.4.2 setup_gdt
```s
setup_gdt:
	lgdt gdt_descr
	ret
```
##### 3.2.4.2.1 gdt_descr 描述符表寄存器（/GDT）初始化数据
对应的gdt_descr:
```s
.align 2 # 2^2=4字节对齐
.word 0  # 2字节，用于明确强制对齐。2+6(下面的gdt_descr)=8字节对齐
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long gdt		# magic number, but it works for me :^)
```
##### 3.2.4.2.2 gdt初始化
对应的gdt
.quad 表示 64 位立即数
```s
gdt:	
    # 第一个NULL描述符，必须为0
    .quad 0x0000000000000000	/* NULL descriptor */
    # 第二个描述符，代码段描述符
	.quad 0x00c09a0000000fff	/* 16Mb */
    # 第三个描述符，数据段描述符
	.quad 0x00c0920000000fff	/* 16Mb */
    # 第四个描述符，系统段描述符，这暂时不用。这样设置也许是为了让下面剩下的构成偶数。
	.quad 0x0000000000000000	/* TEMPORARY - dont use */
    # 这是最核心的，定义了gdt表中的剩下的252个项目，也说明了是给LDT和TSS预留的空间
	.fill 252,8,0			    /* space for LDT''s and TSS''s etc */
```
### 3.2.5. 分页表预留空间

`分页表`是用来将`虚拟地址`映射到`物理地址`的。分页表的大小是`4KB`，每个项是`8字节`，所以分页表有`512个项`。

> 下面是页表，不包含页目录项。

```s
.org 0x1000 # 为啥这里是0x1000，不是0x0000？因为前面有一个页目录项！占用了0x0000-0x1000, 4KB
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
```

- 1. 背景：

**x86 分页机制**中，页目录项（4 字节）指向页表，页表（4KB=0x1000 字节）包含 1024 个页表项（4 字节），每个页表项映射 4KB 物理内存；

- 2. .org xxx：
告诉汇编器,"从这里开始，接下来的代码/数据应该放置在指定的地址xxx"。

- 3. 预留页表：
预留 4 个页表（pg0~pg3），`每个 4KB`，地址范围 0x1000~0x4FFF：

- 4. 页表映射及总内存大小
4 个页表 × (1024 项 / 页表) × (4KB / 项) =`16MB 物理内存映射`，这是 Linux 0.11 的`默认内存支持上限`；

- 5. 避开页表空间，为后续的内存设置做铺垫
.org 0x5000：将后续数据对齐到 0x5000，避开页表空间。

### 3.2.6 临时软盘缓冲区预留空间
```s
tmp_floppy_area:
	.fill 1024,1,0
```

- 1.tmp_floppy_area：

**软盘驱动的临时 DMA 缓冲区**，大小 1KB
>（.fill 1024,1,0：填充 1024 个字节，每个值为 0）；

- 2.设计原因：

早期软盘 DMA 控制器的寻址范围有限，无法访问某些内存缓冲区，需通过该临时区域中转；

- 3.对齐要求：

避开 64KB 边界，防止 DMA 寻址跨边界导致错误。

### 3.2.7 设置好分页，利用出栈进入main，来到C的世界

```s
after_page_tables:
	pushl $0		# These are the parameters to main :-)
	pushl $0
	pushl $0
	pushl $L6		# return address for main, if it decides to.实际上，永远不会执行这一步！因为main函数不会返回！
	pushl $main
	jmp setup_paging # 先设置好分页，CPU再进入利用出栈进入main。这里没有使用jmp等跳转指令，而是直接利用出栈进入main。奇淫技巧！
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.
```

#### 3.2.7.1 为啥main的函数参数有3个0？

这是为了遵循 C函数调用约定（cdecl调用约定）

历史原因：早期的Unix内核main函数确实有参数：
```c
// 在某些Unix版本中
void main(struct boot_params *bp, int argc, char **argv)
```
而当初linus是基于unix标准的，所以保留了这3个参数。

好了，那我们先看看setup_paging的具体实现

#### 3.2.7.2 setup_paging 分页机制初始化
下面就是正式进入main之前，执行的最后一段函数-->设置分页模式！
在讲述源码之前，我们肯定要先掌握页面的结构：

![页目录项、页表项的结构图](../img/setup/PDE、PTE.png)

PDE 是**页目录表中的条目**，用于定位**页表**的**物理地址**。
PTE 是**页表中的条目**，用于定位**物理页**的**物理地址**。
>PDE\PTE结构相同。
所以，页表、页目录表就是一个存储页表项的数组。

PDE\PTE核心字段如下（从高位到低位，对应图中 31~0 位）：
 
| 字段 | 位位置 | 核心解释 |
|------|--------|----------|
| **页表地址** | 31~12 | 存储**页表的物理基地址**（页表按4KB对齐，**低12位为0，仅高20位为有效地址**），CPU据此找到**对应页表**。 |
| **AVL** | 11~9 | 操作系统/软件自定义位，硬件不使用，可用于存储页表管理的额外信息。 |
| **G** | 8 | 全局位，置1时该页表项对应的页为**全局页**，TLB刷新时不失效（用于内核等全局共享页）。 |
| **0** | 7 | 保留位，必须为0，硬件未定义用途。 |
| **D** | 6 | 脏位，置1表示该页表**已被修改**（页表本身的写操作，非页内数据），仅PDE有此位。 |
| **A** | 5 | 访问位，置1表示该页表**已被访问**（CPU读取PDE时自动置1），用于页替换算法。 |
| **PCD** | 4 | 页缓存禁用位，置1时该页表**不使用CPU缓存**，直接访问内存（用于MMIO等不可缓存区域）。 |
| **PWT** | 3 | 页写通位，置1时该页表采用**写通缓存策略**（写操作同时更新缓存和内存），0为写回策略。 |
| **U/S** | 2 | 用户/超级用户位，置1时**用户态程序可访问**该页表，0仅内核态（超级用户）可访问。 |
| **R/W** | 1 | 读写位，置1时该页表**可读写**，0为只读（写操作触发页异常）。 |
| **P** | 0 | 存在位，置1表示该**页表已加载到物理内存**，0表示页表不在内存（触发缺页异常）。 |


显然我们的低3位分别是：

- 低1位：P位，-present bit，是否存在。
- 低2位：R/W位，read/write bit，是否可读可写。
- 低3位：U/S位，user/supervisor bit，是否用户级或超级级。

这后面，我们通过+7来完成权限设置，实际上就是+0b111


>注意stosl指令：表示将 EAX（4 字节）写入 EDI 指向的地址并让 EDI+4；

##### 3.2.7.2.1 将1个页目录项、4个页表项全部清零
```s
.align 2                # 设置4字节对齐
setup_paging:           # 一目了然了，开始设置分页
	movl $1024*5,%ecx		#注释一目了然，5个页面，每个1KB，1个页目录表+4个页表/*5 pages - pg_dir+4 page tables */
	xorl %eax,%eax          # 这是清空eax寄存器，准备用来填充页目录项
	xorl %edi,%edi			# pg_dir is at 0x0000 
	cld;rep;stosl           # 连续填充 0，cld表示地址递增，rep表示循环 ECX 次，stosl表示将 EAX（4 字节）写入 EDI 指向的地址并让 EDI+4；执行后，页目录（0x0000~0x0FFF）和 4 个页表（0x1000~0x4FFF）全部被置为 0。
```
##### 3.2.7.2.2 将 1-4个页表全部+0b111，赋权
```s
    # 下面+7其实是加上0b111，设置成特定属性。pg0就是第一个页表。
    # 把第1个页表的地址+7，写入页目录表的第1个条目
	movl $pg0+7,pg_dir		    /* set present bit/user r/w */
    # 把第2个页表的地址+7，写入页目录表的第2个条目
    # +4代表着页目录表的第2个条目，4字节一个条目，所以+4就是第2个页表的条目
	movl $pg1+7,pg_dir+4		/*  --------- " " --------- */
    # 把第3个页表的地址+7，写入页目录表的第3个条目
	movl $pg2+7,pg_dir+8		/*  --------- " " --------- */
    # 把第4个页表的地址+7，写入页目录表的第4个条目
	movl $pg3+7,pg_dir+12		/*  --------- " " --------- */
    # 下面开始设置页表项，把物理内存地址映射到虚拟地址0xfff000~0xfffff
```

##### 3.2.7.2.3 设置页表写入方向(倒向)与起始地址。
```s
    # 把pg3页表最后一个页表项的地址，赋值给EDI寄存器（准备写入）
	movl $pg3+4092,%edi
    # 把「物理地址0xFFF000（16MB-4KB） + 权限位7」赋值给EAX寄存器
    # 这里就是页表和物理地址的映射
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std                     # 设方向标志位 DF=1，使 stosl 指令从高地址向低地址存储
```

这里仅仅只有3条指令，但却奠定了pg3页表与物理地址的映射，具体如下：
pg3的最后一个页表，对应的内容为：0x00fff007
结合页表结构：

![页表结构](../img/setup/PDE、PTE.png)

|结构|位数bit|值|
|-|-|-|
|页表地址|20|00fff|
|AVL位|3|0|
|G位|1|0|
|0位|1|0|
|D位|1|0|
|A位|1|0|
|PCD位|1|0|
|PWT位|1|0|
|U/S位|1|1|
|R/W位|1|1|
|P位|1|1|

>按照**物理地址=页表地址<<12+页表项偏移量**，计算出物理地址。只要我们给出页表项偏移量，就可以计算出物理地址。

>所以说，页表项的高20位就是物理页面的基地址，还要加上指定的线性地址的低12位，才能得到最终的物理地址。】

>这里就是整个物理页面和页表之间的对应关系！通透！！！！！！！！

##### 3.2.7.2.4 填充pg3的剩余页表项

stosl指令：

- 作用：将 EAX（4 字节）写入 EDI 指向的地址并让 EDI+4；
- 方向标志位 DF=1：表示从高地址向低地址存储；
- 方向标志位 DF=0：表示从低地址向高地址存储；

```s
1:	stosl			    # 倒序填充，更高效
	subl $0x1000,%eax   # 把 EAX 减去 0x1000（4KB），准备写入下一个页表项
	jge 1b              #「大于等于则跳转」到1，填充剩余页表项
	cld                 # 重置方向标志位 DF=0，使 stosl 指令恢复为从低地址向高地址存储
```

这里更加有趣，pg3最后一个页表值为0x00fff007，执行了后，eax减去0x1000，就变成了0x00ffe007，继续填充，直到eax为0x00000007，填充完成(**再减少，就小于1了，当然停止了！**)。

我们看看对应填充的页表地址为：
0x00ffe,0x00ffd,0x00ffc 依次减小！

>这里就是整个物理页面和页表之间的对应关系！

##### 3.2.7.2.5 开启分页机制

>页表的地址存储在CR3寄存器里面！

下面是完成「填充剩余页表项→写入 CR3 寄存器→置位 CR0 开启分页」的关键步骤

这段代码只负责 pg3 页表的完整填充，其他页表（pg0/pg1/pg2）的填充逻辑在这段代码之外
```s
    # 开启分页机制
	xorl %eax,%eax	#把%eax清零	cause pg_dir is at 0x0000 */
	movl %eax,%cr3	#页目录基址寄存器（CR3），CPU 会从这个寄存器读取「页目录表的物理起始地址」
	movl %cr0,%eax  # 把CR0寄存器的值，赋值给EAX寄存器（准备修改）
	orl $0x80000000,%eax # 给 % eax 的第 31 位（最高位）置 1；为后续开启PG位，做准备！or或命令，更加高效！

    # 真正的开启，就这一条指令
	movl %eax,%cr0	# 把eax值写回 CR0，分页机制正式生效！！所以，最后一步才是开启PG！
	ret			# 返回，继续执行 main 函数
```

### 3.2.7.1 手动模拟压栈，出栈进入main
#### 3.2.7.1.1 setup_paging 执行后的关键动作
##### 3.2.7.1.1.1 压栈动作
这里的一系列压栈 ，手动实现了将main函数的参数和返回地址，压入栈中。

>一般是需要call指令，才能自动实现压栈动作。但这里手动实现了。

```s
after_page_tables:
    pushl $0		# These are the parameters to main :-)
    pushl $0
    pushl $0
    pushl $L6		# return address for main, if it decides to.实际上，永远不会执行这一步！因为main函数不会返回！
    pushl $main
```

##### 3.2.7.1.1.2 触发导火线---ret 指令的作用
setup_paging的最后一行是ret指令，这个ret是整个跳转的核心：

setup_paging执行完所有分页初始化逻辑后，执行ret；

ret指令会：

>1.从栈顶弹出第一个值（即main的入口地址）,并把这个地址赋值给eip（CPU 的指令指针寄存器）；

>2.CPU 立即跳转到main的入口地址执行，同时栈顶自动弹出main地址，栈内容变为L6 → 0 → 0 → 0（刚好匹配main的参数和返回地址）；

>3.main执行时，会按照 x86 函数调用约定，从栈中取出 3 个 0 作为参数，执行完若调用ret，则会弹出L6并跳转到L6（死循环）。但是main函数不会退出！一直for(;;)死循环！所以L6形同虚设。