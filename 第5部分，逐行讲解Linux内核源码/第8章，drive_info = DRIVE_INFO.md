# 1.讲解
这是我们main函数![主线，main函数](./main.c)里面的第二行：
```c
 	drive_info = DRIVE_INFO;
```
同样，把两边的变量都讲解清楚即可：

# 2.dirve_info讲解
## 2.1.定义：
dirve_info是存储`硬盘驱动器`的`硬件参数信息`的`全局变量`,本质上是一个包含32字符数组的结构体！

它的值来源于 BIOS 在系统启动时`检测到的硬盘物理特性`，对后续内核的`硬盘驱动`（hd.c）至关重要。

## 2.2.位置：
位于![主线，main函数](./main.c)中，定义如下：
```c
struct drive_info { char dummy[32]; } drive_info;
```

# 3.DRIVE_INFO讲解
## 3.1.定义：
DRIVE_INFO是一个指向`drive_info`结构体的`指针`，它的地址是`0x90080`！

```c
#define DRIVE_INFO (*(struct drive_info *)0x90080)
```

显然，这个和我们前面的`ROOT_DEV=ORIG_ROOT_DEV;`是一个道理！

本质就是setup阶段，将元数据从0x00000保存到0x90000处后，**读取最新地址的元数据的硬件基本信息。**

具体详解可以看这篇文章：[text](第5章，setup.S.md#3236-获取硬盘-0hd0参数将其从-bios-表复制到-0x90080)

# 4.总结
这句代码的作用是将硬盘参数信息从预定义的位置（DRIVE_INFO）复制到**内核变量drive_info**中。DRIVE_INFO是在引导加载程序阶段由setup.S设置的，包含了硬盘的几何参数，如**柱面数、磁头数和扇区数**等。这些信息对于内核正确地与硬盘进行交互和管理存储设备是必不可少的。