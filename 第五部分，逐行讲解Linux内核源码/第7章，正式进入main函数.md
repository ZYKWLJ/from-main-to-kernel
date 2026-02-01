# 1.前情回顾
前面我们说到，操作系统经过bootloader阶段，也就是具体来说，经过了bootsect、setup、head阶段，正式从磁盘加载到了内存，并开启了保护模式、分页模式，进入了内核main之中。

而我们知道，真正体现操作系统设计内核的是main函数里面的各个模块，比如我们常常说的，内存管理、进程管理、文件系统、设备驱动等。

万丈高楼平地起，这才哪到哪呢！小伙子，如果你只是想走马观花，那你就干脆别学了！现在就放弃！
如果你还想继续深入Linux内核设计的艺术，那就跟着我们继续走！

**下面是一场硬仗，亮剑！**

# 2.main函数的全貌
head.s之后，进入main执行，少说废话，直接贴出main函数的全貌：

```c
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
```
# 3.main函数的详细解析
为保证原汁原味，我们先从注释讲解。
## 3.1 先解析所有注释
### 3.1.1 第一处注释
```c
void main(void)		/* This really IS void, no error here. */
{			/* The startup routine assumes (well, ...) this */
}
```
第一处注释含义为：标准C中，main不推荐返回void，推荐返回int，但是这是内核，永远都不会退出，所以void也是合法的

第二处注释含义为：由汇编跳转到这里。

### 3.1.2 第二处注释
```c
/*
 * Interrupts are still disabled. Do necessary setups, then
 * enable them
 */
```
含义为：

此时**中断还没有开启**，接下来要做一些**必要的初始化**，然后**再开启中断**。
可预见，接下来会进行一系列初始化操作。

### 3.1.3 第三处注释
```c
/*
 *   NOTE!!   For any other task 'pause()' would mean we have to get a
 * signal to awaken, but task0 is the sole exception (see 'schedule()')
 * as task 0 gets activated at every idle moment (when no other tasks
 * can run). For task0 'pause()' just means we go check if some other
 * task can run, and if not we return here.
 */
```
含义为：

其他任务调用pause()时，会**阻塞当前任务**，**等待信号唤醒**。但是**task0例外**，因为task0会在**每个空闲 moment 被激活**（当其他任务都不能运行时）。因此，task0 调用 pause() 时，**只是检查是否有其他任务可以运行，如果没有，就返回这里。**

>也就是task0是兜底的任务！这确保了**系统在没有其他任务可运行时，也能正常工作。**
