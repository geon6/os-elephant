#ifndef __USERPROG_PROCESS_H
#define __USERPROG_PROCESS_H

#include "thread.h"
#include "stdint.h"
#define default_prio 31
#define USER_STACK3_VADDR (0xc0000000 - 0x1000) // 0xc0000000是用户空间最高处, 剪掉一个page的大小之后, 就是这个page的起始地址. 3表示特权级为3
#define USER_VADDR_START 0x8048000

// 用户进程工作在特权级3, 需要从特权级0(系统线程)转为特权级3
// 实现方式为中断, 大致思路为: 
// kernel.S中的intr_exit用于退出中断, 它会恢复上下文. 
// 抢在它恢复上下文之前, 修改上下文信息. 就可以进入特权级3
// 上下文信息存储在pcb的struct intr_stack中

/*
其他细节:
1. eflags的IF位为1: 保证能够中断. 
2. elfags的IOPL位为0, 表示不允许访问硬件(用户进程不能访问硬件, os才能访问). IOPL(IO Privilege Level)有两位, 可以表示0, 1, 2, 3, 用于控制对io端口的访问权限.
3. 需要在intr_stack中存储CS选择子, 并指定RPL为3. Code Segment Selector用来选择代码段, RPL(Request Privilege Level)用来指定访问当前代码段所需的特权级. 中断返回后, CPU的特权级恢复为CS的RPL.
4. intr_stack中的段寄存器的选择子指向DPL为3的内存段. DPL(Descriptor Privilege Level)

关于DPL, RPL, CPL的区别与联系: https://www.cnblogs.com/cindycindy/p/13489402.html
*/

void process_execute(void* filename, char* name);
void start_process(void* filename_);
void process_activate(struct task_struct* p_thread);
void page_dir_activate(struct task_struct* p_thread);
uint32_t* create_page_dir();
void create_user_vaddr_bitmap(struct task_struct* user_prog);

#endif