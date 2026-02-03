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

这里应该是Linux0.11的BUG，在0.12里面，就改成了:

```c
#define CODE_SPACE(addr) ((((addr)+4095)&~4095) < \
 current->end_code)
```

因为如果不是end_code，这里说不过去。

### 1.2.5 copy_page(from,to) 


### 1.2.6 unsigned long get_free_page(void)

### 1.2.7 void free_page(unsigned long addr)

### 1.2.8 int free_page_tables(unsigned long from,unsigned long size)


### 1.2.9 int copy_page_tables(unsigned long from,unsigned long to,long size)



### 1.2.10 unsigned long put_page(unsigned long page,unsigned long address)

### 1.2.11 void un_wp_page(unsigned long * table_entry)

### 1.2.12 void do_wp_page(unsigned long error_code,unsigned long address)

### 1.2.13 void write_verify(unsigned long address)

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
