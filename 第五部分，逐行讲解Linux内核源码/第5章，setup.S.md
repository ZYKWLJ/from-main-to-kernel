
# 1.前情回顾
好了，我们上面完成了main函数里面的第一行代码：
```
ROOT_DEV = ORIG_ROOT_DEV;
```
的讲解，并在其中开枝散叶，完成了bootsect.s的讲解。在其中也说到，Linux的整体的执行流程为：bootsect->setup->head->main()。
但是本书是为了以**main为主线讲解**，我们这里其实是跳过了**setup.s、head.s**,直接来到了main的第一行代码。

但是，直接讲解setup.s、head.s其实是有点强行灌知识的意味，让人不适。于是，我绕了个弯子，把大的面给你说明白了，再来讲小的过程，你就会明白的多。

好了，接下来我们就来补全跳过的setup.s和head.s。

首先我们来看看setup.s的代码吧。

>我们还是遵循老方法，先讲清楚作用，在深挖作用背后的原理，最后在回扣源码，这样比强行灌输源码更容易理解。

# 2.setup.s功能及解释

## 2.1 功能
setup.s的功能很简单，就是完成一些元数据信息的保存，然后把CPU切换到保护模式下去。

1. 将重要的元信息，如光标位置等，文字显示模式等从0地址处复制到0x90000地址处。

2. 将system模块从0x10000地址处复制到0x00000地址处。

3. 从16位实模式切换到32位保护模式(需要提前设置好GDT)。

4. 跳转到0x00000地址处，开始执行system模块的代码。

## 2.2 解释

### 0. BIOS的功能再议
BIOS除了加载引导区外，还涉及了一些其他的功能，如：

- 初始化硬件设备（如磁盘、键盘、显示器等）

- 加载引导程序（如 GRUB）

- 提供 BIOS 中断服务（如打印字符、读取磁盘等）

而这些硬件信息会被保存在低地址处，中断服务就是调用这些硬件信息的入口，你可以把它看成一个函数，仅此而已。

### 1. 将重要的元信息，如光标位置等，文字显示模式等从0地址处复制到0x90000地址处。

开机后到 setup 阶段，所有硬件（显示器、磁盘、键盘）**都由 BIOS 统一管理（准确的说，BIOS启动后，会接管这些硬件）**，**光标位置**（第几行第几列）、**文字显示模式**（如 80×25 字符模式、显示分辨率、颜色属性）、**磁盘参数**（每磁道扇区数）等硬件状态信息，都由 BIOS **维护在低地址内存**（0 地址段，**BIOS 数据区**） 中。而内核启动后会**完全接管所有硬件的管理权**（**不再依赖 BIOS 中断**），如果丢失这些状态信息，内核将无法知道**当前显示器的工作模式、光标在哪**，后续无法正常打印内核日志、响应用户输入。

那么为什么需要将这些元数据信息复制0x90000地址处呢？一个大胆的猜测就是后面低地址处的数据会被覆盖，这一步就是覆盖之前的信息保留，供后续内核使用。

实际上，也确实如此，因为，setup执行的第二步，就是将我们的system模块移动到低地址处，完成覆盖！一切都说的通了！

### 2. 将system模块从0x10000地址处复制到0x00000地址处.
为什么需要将system模块从0x10000地址处复制到0x00000地址处？

这是因为，system 内核编译链接时，**编译器 / 链接器默认内核会在 0x00000 运行**，因此**代码中所有的全局变量、函数调用、内存引用都是「基于 0x00000 的偏移地址」**（比如函数main的地址是 0x0000xxxx）。如果直接在 0x10000 运行，**内核会因为「地址引用错误」直接崩溃**（比如想访问 0x00001234，实际访问到 0x10000+0x1234，读取到垃圾数据）。


### 3. 从16位实模式切换到32位保护模式(需要提前设置好GDT)。
这是setup的关键，我们要把这点搞清楚不容易，慢慢来，先从CPU执行模式说起。

什么是CPU的执行模式？**就是CPU如何获得数据的物理地址的？** 这个简简单单的问题，实际涉及了悠久的历史。

在讲解各种模式之前，我们先来介绍一下CPU底层的模式切换原理。

#### 3.1 保存CPU重要元信息的CR0寄存器

CPU中有一个特别重要的寄存器——CR0寄存器。

> 开门见山，CPU 有3种执行模式：实模式、保护模式和分页模式。这三种模式涉及CPU的重要寄存器——**状态字寄存器CR0**的两个位：**PE、PG**。

![CR0寄存器](../img/setup/CR0寄存器.png)

|PE|PG|CPU执行模式|
|-|-|-|
|0|0|实模式（Real Mode）|
|1|0|保护模式（Protected Mode）|
|1|1|分页模式（Paging Mode）|

- **PE（Protection Enable）**：**保护使能位**，控制 CPU 是处于实模式还是**保护模式**。当 PE=0 时，CPU 处于实模式；当 PE=1 时，CPU 进入保护模式。

- **PG（Paging Enable）**：**分页使能位**，控制 CPU 是否启用**分页机制**。当 PG=0 时，CPU 不使用分页机制；当 PG=1 时，CPU 启用分页机制，进入分页模式。

> 所以我们看到，所谓CPU的各种执行模式，不过就是这几个位的开关而已，那么2^2=4才对，为啥只有3猴子那个模式？因为分页模式的基础是打开了保护模式。


#### 3.1 实模式
实模式是 CPU 的**初始工作模式**，也是最简单的模式。在实模式下，**CPU 直接使用物理地址进行内存访问**，没有任何内存保护机制。实模式的内存寻址方式是通过**段寄存器和偏移地址**的组合来计算物理地址，**最大可寻址 1MB 内存（20 位地址总线）。**

又因为CPU按照pc寄存器里面的值作为执行地址，也就是说，对应关系为：

>pc=f(cs,ip) = cs << 4  + ip


#### 3.2 保护模式




#### 3.3 分页模式




实模式下，CPU 只能访问 1MB 内存，并且没有内存保护机制，无法利用现代 CPU 的特性（如分页、虚拟内存、多任务等）。而 Linux 内核需要使用这些高级特性，因此必须切换到保护模式。


### 4. 跳转到0x00000地址处，开始执行system模块的代码。

这是理所当然的，执行流就是BIOS->bootsect->setup->system

# 3. setup.S源码讲解

## 3.1 setup.S全部源码
```s
	.code16
	.equ INITSEG, 0x9000	# we move boot here - out of the way
	.equ SYSSEG, 0x1000	# system loaded at 0x10000 (65536).
	.equ SETUPSEG, 0x9020	# this is the current segment

	.global _start, begtext, begdata, begbss, endtext, enddata, endbss
	.text
	begtext:
	.data
	begdata:
	.bss
	begbss:
	.text

	ljmp $SETUPSEG, $_start	
_start:
	mov %cs,%ax
	mov %ax,%ds
	mov %ax,%es

# print some message

	mov $0x03, %ah
	xor %bh, %bh
	int $0x10

	mov $29, %cx
	mov $0x000b,%bx
	mov $msg2,%bp
	mov $0x1301, %ax
	int $0x10
# ok, the read went well so we get current cursor position and save it for
# posterity.
	mov	$INITSEG, %ax	# this is done in bootsect already, but...
	mov	%ax, %ds
	mov	$0x03, %ah	# read cursor pos
	xor	%bh, %bh
	int	$0x10		# save it in known place, con_init fetches
	mov	%dx, %ds:0	# it from 0x90000.
# Get memory size (extended mem, kB)

	mov	$0x88, %ah 
	int	$0x15
	mov	%ax, %ds:2

# Get video-card data:

	mov	$0x0f, %ah
	int	$0x10
	mov	%bx, %ds:4	# bh = display page
	mov	%ax, %ds:6	# al = video mode, ah = window width

# check for EGA/VGA and some config parameters

	mov	$0x12, %ah
	mov	$0x10, %bl
	int	$0x10
	mov	%ax, %ds:8
	mov	%bx, %ds:10
	mov	%cx, %ds:12

# Get hd0 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x41, %si
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0080, %di
	mov	$0x10, %cx
	rep
	movsb

# Get hd1 data

	mov	$0x0000, %ax
	mov	%ax, %ds
	lds	%ds:4*0x46, %si
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	rep
	movsb

## modify ds
	mov $INITSEG,%ax
	mov %ax,%ds
	mov $SETUPSEG,%ax
	mov %ax,%es

##show cursor pos:
	mov $0x03, %ah 
	xor %bh,%bh
	int $0x10
	mov $11,%cx
	mov $0x000c,%bx
	mov $cur,%bp
	mov $0x1301,%ax
	int $0x10

#show detail
	mov %ds:0 ,%ax
	call print_hex
	call print_nl

#show memory size
	mov $0x03, %ah
	xor %bh, %bh
	int $0x10
	mov $12, %cx
	mov $0x000a, %bx
	mov $mem, %bp
	mov $0x1301, %ax
	int $0x10

##show detail
	mov %ds:2 , %ax
	call print_hex

#show 
	mov $0x03, %ah
	xor %bh, %bh
	int $0x10
	mov $25, %cx
	mov $0x000d, %bx
	mov $cyl, %bp
	mov $0x1301, %ax
	int $0x10
 
	mov %ds:0x80, %ax
	call print_hex
	call print_nl

 
	mov $0x03, %ah
	xor %bh, %bh
	int $0x10
	mov $8, %cx
	mov $0x000e, %bx
	mov $head, %bp
	mov $0x1301, %ax
	int $0x10
 
	mov %ds:0x82, %ax
	call print_hex
	call print_nl

 
	mov $0x03, %ah
	xor %bh, %bh
	int $0x10
	mov $8, %cx
	mov $0x000f, %bx
	mov $sect, %bp
	mov $0x1301, %ax
	int $0x10
 
	mov %ds:0x8e, %ax
	call print_hex
	call print_nl
 
	mov	$0x01500, %ax
	mov	$0x81, %dl
	int	$0x13
	jc	no_disk1
	cmp	$3, %ah
	je	is_disk1
no_disk1:
	mov	$INITSEG, %ax
	mov	%ax, %es
	mov	$0x0090, %di
	mov	$0x10, %cx
	mov	$0x00, %ax
	rep
	stosb
is_disk1:

# now we want to move to protected mode ...

	cli			# no interrupts allowed ! 

# first we move the system to its rightful place

	mov	$0x0000, %ax
	cld			# 'direction'=0, movs moves forward
do_move:
	mov	%ax, %es	# destination segment
	add	$0x1000, %ax
	cmp	$0x9000, %ax
	jz	end_move
	mov	%ax, %ds	# source segment
	sub	%di, %di
	sub	%si, %si
	mov 	$0x8000, %cx
	rep
	movsw
	jmp	do_move

# then we load the segment descriptors

end_move:
	mov	$SETUPSEG, %ax	# right, forgot this at first. didnt work :-)
	mov	%ax, %ds
	lidt	idt_48		# load idt with 0,0
	lgdt	gdt_48		# load gdt with whatever appropriate


	inb     $0x92, %al	# open A20 line(Fast Gate A20).
	orb     $0b00000010, %al
	outb    %al, $0x92

	mov	$0x11, %al		# initialization sequence(ICW1)
					# ICW4 needed(1),CASCADE mode,Level-triggered
	out	%al, $0x20		# send it to 8259A-1
	.word	0x00eb,0x00eb		# jmp $+2, jmp $+2
	out	%al, $0xA0		# and to 8259A-2
	.word	0x00eb,0x00eb
	mov	$0x20, %al		# start of hardware int's (0x20)(ICW2)
	out	%al, $0x21		# from 0x20-0x27
	.word	0x00eb,0x00eb
	mov	$0x28, %al		# start of hardware int's 2 (0x28)
	out	%al, $0xA1		# from 0x28-0x2F
	.word	0x00eb,0x00eb		#               IR 7654 3210
	mov	$0x04, %al		# 8259-1 is master(0000 0100) --\
	out	%al, $0x21		#				|
	.word	0x00eb,0x00eb		#			 INT	/
	mov	$0x02, %al		# 8259-2 is slave(       010 --> 2)
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0x01, %al		# 8086 mode for both
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1
	.word	0x00eb,0x00eb
	mov	$0xFF, %al		# mask off all interrupts for now
	out	%al, $0x21
	.word	0x00eb,0x00eb
	out	%al, $0xA1


	mov	%cr0, %eax	# get machine status(cr0|MSW)	
	bts	$0, %eax	# turn on the PE-bit 
	mov	%eax, %cr0	# protection enabled
				
				# segment-descriptor        (INDEX:TI:RPL)
	.equ	sel_cs0, 0x0008 # select for code segment 0 (  001:0 :00) 
	ljmp	$sel_cs0, $0	# jmp offset 0 of code segment 0 in gdt

empty_8042:
	.word	0x00eb,0x00eb
	in	$0x64, %al	# 8042 status port
	test	$2, %al		# is input buffer full?
	jnz	empty_8042	# yes - loop
	ret

gdt:
	.word	0,0,0,0		# dummy

	.word	0x07FF		# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		# base address=0
	.word	0x9A00		# code read/exec
	.word	0x00C0		# granularity=4096, 386

	.word	0x07FF		# 8Mb - limit=2047 (2048*4096=8Mb)
	.word	0x0000		# base address=0
	.word	0x9200		# data read/write
	.word	0x00C0		# granularity=4096, 386

idt_48:
	.word	0			# idt limit=0
	.word	0,0			# idt base=0L

gdt_48:
	.word	0x800			# gdt limit=2048, 256 GDT entries
	.word   512+gdt, 0x9		# gdt base = 0X9xxxx, 
	# 512+gdt is the real gdt after setup is moved to 0x9020 * 0x10
print_hex:
	mov $4,%cx
	mov %ax,%dx

print_digit:
	rol $4,%dx	#循环以使低4位用上，高4位移至低4位
	mov $0xe0f,%ax #ah ＝ 请求的功能值，al = 半个字节的掩码
	and %dl,%al
	add $0x30,%al
	cmp $0x3a,%al
	jl outp
	add $0x07,%al

outp:
	int $0x10
	loop print_digit
	ret
#打印回车换行
print_nl:
	mov $0xe0d,%ax
	int $0x10
	mov $0xa,%al
	int $0x10
	ret

msg2:
	.byte 13,10
	.ascii "Now we are in setup ..."
	.byte 13,10,13,10
cur:
	.ascii "Cursor POS:"
mem:
	.ascii "Memory SIZE:"
cyl:
	.ascii "KB"
	.byte 13,10,13,10
	.ascii "HD Info"
	.byte 13,10
	.ascii "Cylinders:"
head:
	.ascii "Headers:"
sect:
	.ascii "Secotrs:"
.text
endtext:
.data
enddata:
.bss
endbss:
```
