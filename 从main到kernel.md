# 序言（本书目的）
本书，从main.c开始，逐行代码讲解Linux内核。
为啥从main.c开始分析？很简单，因为真正的内核是从main.c开始执行的。

废话少说，直接贴上main.c的源码。
```c
/*
 *  linux/init/main.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>
#include <time.h>

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
static inline fork(void) __attribute__((always_inline));
static inline pause(void) __attribute__((always_inline));
static inline _syscall0(int,fork)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,void *,BIOS)
static inline _syscall0(int,sync)

#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <asm/system.h>
#include <asm/io.h>

#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include <linux/fs.h>

static char printbuf[1024];

extern int vsprintf();
extern void init(void);
extern void blk_dev_init(void);
extern void chr_dev_init(void);
extern void hd_init(void);
extern void floppy_init(void);
extern void mem_init(long start, long end);
extern long rd_init(long mem_start, int length);
extern long kernel_mktime(struct tm * tm);
extern long startup_time;

/*
 * This is set up by the setup-routine at boot-time
 */
#define EXT_MEM_K (*(unsigned short *)0x90002)
#define DRIVE_INFO (*(struct drive_info *)0x90080)
#define ORIG_ROOT_DEV (*(unsigned short *)0x901FC)

/*
 * Yeah, yeah, it's ugly, but I cannot find how to do this correctly
 * and this seems to work. I anybody has more info on the real-time
 * clock I'd be interested. Most of this was trial and error, and some
 * bios-listing reading. Urghh.
 */

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)

static void time_init(void)
{
	struct tm time;

	do {
		time.tm_sec = CMOS_READ(0);
		time.tm_min = CMOS_READ(2);
		time.tm_hour = CMOS_READ(4);
		time.tm_mday = CMOS_READ(7);
		time.tm_mon = CMOS_READ(8);
		time.tm_year = CMOS_READ(9);
	} while (time.tm_sec != CMOS_READ(0));
	BCD_TO_BIN(time.tm_sec);
	BCD_TO_BIN(time.tm_min);
	BCD_TO_BIN(time.tm_hour);
	BCD_TO_BIN(time.tm_mday);
	BCD_TO_BIN(time.tm_mon);
	BCD_TO_BIN(time.tm_year);
	time.tm_mon--;
	startup_time = kernel_mktime(&time);
}

static long memory_end = 0;
static long buffer_memory_end = 0;
static long main_memory_start = 0;

struct drive_info { char dummy[32]; } drive_info;

void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */

 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end);
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();
	move_to_user_mode();
	if (!fork()) {		/* we count on this going ok */
		init();
	}
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
	for(;;) pause();
}

static int printf(const char *fmt, ...)
{
	va_list args;
	int i;

	va_start(args, fmt);
	write(1,printbuf,i=vsprintf(printbuf, fmt, args));
	va_end(args);
	return i;
}

static char * argv_rc[] = { "/bin/sh", NULL };
static char * envp_rc[] = { "HOME=/", NULL };

static char * argv[] = { "-/bin/sh",NULL };
static char * envp[] = { "HOME=/usr/root", NULL };

void init(void)
{
	int pid,i;

	setup((void *) &drive_info);
	(void) open("/dev/tty0",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	printf("%d buffers = %d bytes buffer space\n\r",NR_BUFFERS,
		NR_BUFFERS*BLOCK_SIZE);
	printf("Free mem: %d bytes\n\r",memory_end-main_memory_start);
	if (!(pid=fork())) {
		close(0);
		if (open("/etc/rc",O_RDONLY,0))
			_exit(1);
		execve("/bin/sh",argv_rc,envp_rc);
		_exit(2);
	}
	if (pid>0)
		while (pid != wait(&i))
			/* nothing */;
	while (1) {
		if ((pid=fork())<0) {
			printf("Fork failed in init\r\n");
			continue;
		}
		if (!pid) {
			close(0);close(1);close(2);
			setsid();
			(void) open("/dev/tty0",O_RDWR,0);
			(void) dup(0);
			(void) dup(0);
			_exit(execve("/bin/sh",argv,envp));
		}
		while (1)
			if (pid == wait(&i))
				break;
		printf("\n\rchild %d died with code %04x\n\r",pid,i);
		sync();
	}
	_exit(0);	/* NOTE! _exit, not exit() */
}
```
这里为了保证原汁原味，我保留了main.c的所有代码。但实际上，入口在于main()函数，如下：
这里为了简介，去除了注释。

那么本书的目的很简单，把这个main()函数的执行过程，分析清楚。Linux的执行过程也就一目了然了。
```c
void main(void)		
{			
 	ROOT_DEV = ORIG_ROOT_DEV;
 	drive_info = DRIVE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
	memory_end &= 0xfffff000;
	if (memory_end > 16*1024*1024)
		memory_end = 16*1024*1024;
	if (memory_end > 12*1024*1024) 
		buffer_memory_end = 4*1024*1024;
	else if (memory_end > 6*1024*1024)
		buffer_memory_end = 2*1024*1024;
	else
		buffer_memory_end = 1*1024*1024;
	main_memory_start = buffer_memory_end;
#ifdef RAMDISK
	main_memory_start += rd_init(main_memory_start, RAMDISK*1024);
#endif
	mem_init(main_memory_start,memory_end);
	trap_init();
	blk_dev_init();
	chr_dev_init();
	tty_init();
	time_init();
	sched_init();
	buffer_init(buffer_memory_end);
	hd_init();
	floppy_init();
	sti();
	move_to_user_mode();
	if (!fork()) {		/* we count on this going ok */
		init();
	}
	for(;;) pause();
}
```
后面的每一个章节，都是针对main()函数的每一行代码来的，一行代码一个章节，把Linux内核源码，吃的透透的。

# 第1章，小猴子的故事

> 讲解专精的程度。


本书正式开始前，我给大家讲一个故事，这个故事会贯穿整本书。

老师在课堂演示了Linux操作系统的启动过程，屏幕出现了企鹅。

之后，老师给给小郑，小阳，小康三个人三个布置作业，让他们学习操作系统，并在期末展示学习成果。


小郑比较随意，操作系统嘛，不就是向上为应用程序提供运行环境，向下管理硬件资源吗？这就是操作系统。

小阳则特别谨慎，他理解了操作系统承上启下的作用后，开始专研操作系统的内部实现机制：CPU通过读取指令完成对硬件的控制，数据会加载进CPU内部，CPU会对指令进行译码、解码、执行，最终将屏幕上的内容显示出来。后面，他有深入研究了屏幕显示的原理，屏幕是有一个一个极小的晶体管组成。CPU.......

小康则思考得恰到好处，他理解了操作系统承上启下的作用后，开始专研操作系统的内部实现机制：内存管理、进程管理、文件系统等。关于屏幕点亮的问题，他仅仅一句带过：操作系统启动时，会向屏幕对应的内存映射地址写入特定的数值，屏幕就会点亮起来。

期末展示评分时，老师听完了小郑的答辩，轻轻地摇了摇头；听完小阳的答辩，老师皱起了眉头；听完小康的答辩，老师满意地点了点头。

这个故事告诉我们一个道理：学习操作系统，不能太浅，也不能太深。
如果你想研究透事物的本质，那么最终会走向理论物理学家的道路。这背离了我们学习操作系统的初衷。

我们做一件事，恰到好处就行了，不深不浅，点到为止。你永远也研究不透事情，你只能在一个合适的层面及时收手，然后在这个层面深耕，成为专家。

本书也是按照小康的思路，来讲解操作系统，操作系统的内核运作完全讲解清楚，但是不回去研究诸如CPU为什么能执行指令等等问题。

# 第2章，宏观感受计算机系统

## 2.1 宏观感受计算机系统的各个零件
谈到计算机时，我们脑海中浮现的画面是这样的：

![计算机系统](/img/台式电脑及外设系统.png)

主机、显示屏、鼠标、键盘...
拆开主机，我们会看到：内存、CPU、硬盘、显卡等一些列电子器件：

![主机内部，各个电子器件通过总线相连](/img/拆开主机，总线连接着各个电子器件，相互通信.png)

实际上，这些都叫计算机外设，他们通过主板上的电路相连，并且相互通信。这些连接在一起的物理电子器件，就是我们物理上，通过肉眼能够真实看到的计算机系统。

## 2.2 各个零件间的相互作用——操作系统的降临

但是，这些电子器件仅仅是这个系统的物理组成单元，一个系统的正常运作应该包括组成单元以及各个单元之间相互作用的规则。

而这个相互通信的规则，就是操作系统。

规则？看不见摸不着的，这套规则是以何种物理形态存在的呢？

这套规则，最初以二进制信息的形式存在于硬盘中，后面被加载到内存中后，在内存中管理各个硬件。
二进制信息比较抽象？我也觉得。实际上，这套规则以高低电平的形式存在于内存中。只不过，我们为了方便，将这些高低电平，用0和1来表示。

所以，操作系统就是一套管理硬件的规则，他的物理实体将会在加电后，以高低电平的形式存在于内存中。

那问题来了，这套规则是怎么做出来的呢？**换言之，最初是怎么做出这一堆高低电平来的呢？**

通过敲下一个一个键位，**敲下的动作对应的电流逻辑经过解析后，形成了能够控制硬件高低电平组合。**

并且人类为了方便，使得敲下特定键位，屏幕上能显示对应的字母，例如A、B、C等，这些字母组合最终会形成一份代码。

所以，从这里也看出来，代码这个称号也是逻辑抽象而成的，本质是人类将敲下键位后形成的完整控制硬件的高低电平组合保存在硬件里面，而人类为了方便，将保存的这一套高低电平组合，称之为**代码。**

>这也就解释了，代码这玩意看不见摸不着，为啥就能一直保存并运行？实际上，这个世界上没有代码，只有实际行为(例如敲下键位)产生的物理状态，而这些物理状态，在计算机系统中，就会最终会被解析为高低电平的组合，从而控制硬件的行为。这也就是代码能够产生花里胡哨的功能的本质。

所以你看，计算机中，没有黑魔法，一切都有物理实体。操作系统也不例外。**只不过，我们为了方便，将这些高低电平组合而成的控制逻辑，保存了下来，并叫做操作系统。** 从此便开启了人类统治计算机的序幕。

### 2.3 硬件、软件
>我们之所以害怕软件，是因为这玩意看不见摸不着，但是一旦转化为我们身边熟悉的事物(实际上也确实是)，我们便可以摘下软件的神秘面纱，对它不再恐惧。

>实际上，世界上没有软件，只有硬件，软件最终也是物理硬件行为、状态的抽象，它只不过是为了人类方便操作硬件而发明的词汇罢了。

我们经常会在教科书上看见这样一句话：
计算机系统是由硬件和软件组成的。其实这句话理解起来尚有难度，我们可以转述为：
计算机系统是由硬件及硬件间的相互作用规则组成的。我们这种逻辑的相互作用规则，称为软件。
所以这样翻译一番后，我们对于计算机系统的理解，就会更加清晰。

所以，我们对于软硬件的理解为：
计算机系统的组成是从物理上而言的设备，我们叫它硬件；而各个组成设备间相互作用的规则，是从逻辑上定义的，我们称为软件。

指令是构成软件的最小单元，就是操作硬件的高低电平的一串组合，所以软件也可以叫达到操作特定硬件行为的指令集。

====

### 2.4 在操作系统的管理下，硬件是如何工作的？

由于操作系统定义了硬件间的通信规则，所以，我们也称操作系统为硬件的管理者。

那么，可不可以详细一点呢？操作系统是如何管理这些硬件的呢？

我们说到，计算机系统物理上就是由各个零件和流转与各个零件之间的高低电平组成的，而这些零件之间，通过总线相连，形成了一个整体。



在这些零件里面，有一个最重要的大脑，叫做CPU，内存中的操作系统指令会进入CPU中执行，CPU根据指令，对挂载到总线上的其他硬件进行控制。

同时，所有的设备会对应在内存里面的一个地址，我们称为“内存映射”，也叫做“内存地址空间”，CPU通过操作内存映射，来完成对硬件的控制。同时“内存地址空间”就像一个个小格子，每个格子都有一个唯一的编号，我们称为“内存地址”。

>所以，内存地址空间是一个抽象的逻辑概念，指的是计算机中所有能被 CPU 访问的地址的总和 —— 不仅包含内存条的物理内存地址，还包括显卡、硬盘控制器等硬件设备的映射地址。这些地址的集合，就是内存地址空间。

>那什么是内存寻址空间呢？内存地址空间是针对 CPU 的硬件能力定义的，指 CPU理论上能访问的最大地址范围，由 CPU 的地址总线位数决定。

>简单来说：内存寻址空间是 CPU 的 “最大访问范围”，内存地址空间是实际 “用起来的地址范围”，因此 内存地址空间不会超过内存寻址空间。

>只要CPU能够通过总线访问到设备对应的内存，我们就说，设备挂载到了总线上。

那么总线上挂载了这么多的硬件，CPU怎么知道该操作哪一个呢？


所以理所当然的，总线上应该**有信息唯一的定位到要操作的设备**，我们称为“地址线”。实际上，总线只是一个总称，总线由以下三条分线组成：

- 地址线：用于指定操作的硬件设备

- 控制线：用于指定操作的类型(读、写)

- 数据线：用于传输数据

比如，我现在要给内存地址为A的内存写入数据B，则需要经历如下步骤：

首先明确这肯定是CPU来完成的事，所以

1. 因为写入的内存地址为A，所以将A写入地址线

2. 因为要写入的数据为B，所以将B写入数据线

3. 因为是写入操作，所以将写控制信号写入控制线

而对应于这些物理行为的代码，在intel体系下，就是：

```assembly
mov [A], B
```
---

其实，按照我们正常的逻辑，做一件事应该是：**什么时候，在哪里，对谁，做什么。**

什么时候，这是CPU的运算和计算系统的数据传输的能力决定的，通常转瞬即逝。

在哪里，这就是地址线的作用了，地址线指定了要操作的硬件设备。

对谁，这就是控制线的作用了，控制线指定了要操作的类型(读、写)。

做什么，这就是数据线的作用了，数据线用于传输数据。

>可见，我们的计算机系统也是来源于生活。以生活的角度来看待计算机系统，也就不难理解了。




# 第0章，操作系统启动的大致过程
我们按下开机键后，不一会儿，电脑就启动了。

其实也等同于，操作系统启动成功了，我们可以玩电脑上的游戏了，这些游戏都是运行在操作系统上的。

那么这个过程包含了那些标志性动作呢？




# 第一章，ROOT_DEV = ORIG_ROOT_DEV;

```c
ROOT_DEV = ORIG_ROOT_DEV;
```
## 0.背景解读
一个运行的计算机系统，可以从以下组成部分构成：

### 1.硬件：
- 内存、CPU、硬盘等硬件设备
### 2.软件：
- 操作系统
- 文件系统

具体来说，最开始操作系统和根文件系统都存在于磁盘上，之后操作系统会被加载到内存中去，而根文件系统里面存着所有的文件。CPU通过执行被加载到内存中的操作系统，来管理整个计算机系统的硬件。那么操作系统如何和磁盘交互？这就得通过文件系统。具体来说，操作系统内核里面有一系列数据结构和函数，为访问磁盘提供特定格式，而这些特定格式的数据又存在磁盘的文件系统里面。好了，现在问题来了，操作系统如何知道从哪里开始访问磁盘的文件系统呢？或者，换个说法，
我们要通过那种操作才能让操作系统如何知道从哪里开始访问磁盘的文件系统呢？

- 就是通过上面这段代码：
```c
ROOT_DEV = ORIG_ROOT_DEV;
```
我们先将这个赋值语句两边的变量讲清楚，再来讲解这句话是如何让操作系统达到访问磁盘文件系统的目的。

### 1.1.ORIG_ROOT_DEV
ORIG_ROOT_DEV：这是 Bootloader（setup.S）在内存中留下的一个 **“遗物”**，它记录了根分区(文件系统的起点)在哪里。

什么？有点懵？





### 1.2.ROOT_DEV
ROOT_DEV：这是**内核内部使用的全局变量**，后续代码（如**挂载根文件系统 mount_root()**）会读取这个变量，来决定**去哪个磁盘分区上找 ls、cat等等 程序**。



## 1.作用
简单来说，这段赋值代码的作用是：从 `BIOS / 引导加载程序（Bootloader）`留下的 `“遗产”` 中，`读取用户指定的 “根文件系统设备号”`，并将其赋值给`内核全局变量 ROOT_DEV`。





