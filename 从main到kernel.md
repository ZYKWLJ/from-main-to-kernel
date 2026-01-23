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

# 第-1章，小猴子的故事

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





