#include <stdio.h>

int main() {
    // 定义两个整数，作为加法的操作数
    int a = 10, b = 25;
    
    // 核心：定义寄存器变量sum，强制绑定到eax寄存器（x86架构）
    // unsigned long 改为 int 更贴合加法场景，asm("ax") 等价于 asm("eax")
    register int sum asm("ax");

    // 内嵌汇编：将a+b的结果存入eax（即sum）
    // 逻辑：mov a到eax → add b到eax → 结果留在eax中
    __asm__ __volatile__(
        "movl %1, %%eax\n\t"  // 将变量a的值移入eax寄存器
        "addl %2, %%eax\n\t"  // 将变量b的值加到eax中（eax = a + b）
        : "=a" (sum)          // 输出约束：eax的结果写入sum（=a表示eax是输出）
        : "r" (a), "r" (b)    // 输入约束：a和b存入任意通用寄存器（r=general register）
        : "cc"                // 破坏描述：告诉编译器标志寄存器被修改（add会改标志位）
    );

    // 打印结果
    printf("a = %d, b = %d\n", a, b);
    printf("a + b = %d\n", sum);

    return 0;
}