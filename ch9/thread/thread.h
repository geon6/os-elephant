#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"

// 线程运行的函数, 参数定义为void*后面再转换为对应数据. 跟posix那个差不多
typedef void thread_func(void*);

// 进程or线程状态
enum task_status {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_WAITING,
    TASK_HANGING,
    TASK_DIED
};

// 中断栈 用于保护上下文
// 这个栈在线程自己的内核栈的最顶端
struct intr_stack {
    uint32_t vec_no;    // kernel.S中定义的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy; // 加dummy是因为其实没什么用
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    uint32_t err_code;
    void (*eip) (void);
    uint32_t cs;
    uint32_t eflags;
    void* esp;
    uint32_t ss;
};

// 线程栈
struct thread_stack {
    // 保存这几个寄存器的原因与caller save相似
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    // 线程第一次执行时, eip指向待调用的函数kernel_thread, 其他时候eip指向switch_to的返回地址
    void (*eip)(thread_func* func, void* func_arg);

    // 以下仅供第一次被调度上cpu时使用
    void (*unused_retaddr);
    thread_func* function;
    void* func_arg;
};

// 进程or线程的pcb
struct task_struct {
    uint32_t* self_kstack;      // 内核栈
    enum task_status status;    
    uint8_t priority;           // 优先级
    char name[16];
    uint32_t stack_magic;       // 边界标记, 用于检测栈的溢出
};

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);

#endif
