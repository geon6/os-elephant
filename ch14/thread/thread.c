#include "thread.h"
#include "debug.h"
#include "global.h"
#include "interrupt.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "stdint.h"
#include "string.h"
#include "sync.h"

struct task_struct* idel_thread;      // idle线程
struct task_struct* main_thread;      // 主线程PCB
struct lock pid_lock;                 // pid锁, 用于分配pid
struct list thread_ready_list;        // 就绪队列
struct list thread_all_list;          // 所有任务队列
static struct list_elem* thread_tag;  // 用于保存队列中的线程结点

extern void switch_to(struct task_struct* cur, struct task_struct* next);

// idle线程
static void idle(void* arg UNUSED) {
    while (1) {
        // idle线程的原理: 上来就block自己
        // sti开中断 (其他线程运行完了就中断进行schedule, 所以必须开中断)
        // hlt指令就是什么都不干
        thread_block(TASK_BLOCKED);
        asm volatile("sti; hlt" : : : "memory");
    }
}

struct task_struct* running_thread() {
    uint32_t esp;
    asm("mov %%esp, %0" : "=g"(esp));
    return (struct task_struct*)(esp & 0xfffff000);
}

static void kernel_thread(thread_func* function, void* func_arg) {
    intr_enable();  // 开中断, 有时间中断才能调度其他线程
    function(func_arg);
}

static pid_t allocate_pid() {
    static pid_t next_pid = 0;
    lock_acquire(&pid_lock);
    next_pid++;
    lock_release(&pid_lock);
    return next_pid;
}

// 初始化线程栈thread_stack
void thread_create(struct task_struct* pthread,
                   thread_func function,
                   void* func_arg) {
    // 预留中断栈和内核栈的空间
    pthread->self_kstack -= sizeof(struct intr_stack);
    pthread->self_kstack -= sizeof(struct thread_stack);

    struct thread_stack* kthread_stack =
        (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi =
        kthread_stack->edi = 0;
}

// 初始化线程基本信息
void init_thread(struct task_struct* pthread, char* name, int prio) {
    memset(pthread, 0, sizeof(*pthread));
    pthread->pid = allocate_pid();
    strcpy(pthread->name, name);

    if (pthread == main_thread) {
        pthread->status = TASK_RUNNING;  // 主线程一直运行
    } else {
        pthread->status = TASK_READY;
    }

    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
    pthread->priority = prio;
    pthread->ticks = prio;
    pthread->elapsed_ticks = 0;
    pthread->fd_table[0] = 0;
    pthread->fd_table[1] = 1;
    pthread->fd_table[2] = 2;
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
    pthread->pgdir = NULL;

    // 根目录作为默认工作路径
    pthread->cwd_inode_nr = 0;
    pthread->stack_magic = 0x19870916;
}

// 创建优先级为prio, 名字为name的线程
struct task_struct*
thread_start(char* name, int prio, thread_func function, void* func_arg) {
    // pcb位于内核
    struct task_struct* thread = get_kernel_pages(1);
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);  // 加入就绪队列

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);  // 加入全部线程队列

    return thread;
}

static void make_main_thread() {
    main_thread = running_thread();
    init_thread(main_thread, "main", 31);

    ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
    list_append(&thread_all_list,
                &main_thread->all_list_tag);  // 加入到thread all list
}

void schedule() {
    ASSERT(intr_get_status() == INTR_OFF);

    struct task_struct* cur = running_thread();
    if (cur->status == TASK_RUNNING) {  // cpu时间到了, 加到就绪队列
        ASSERT(!elem_find(&thread_ready_list, &cur->general_tag))
        list_append(&thread_ready_list, &cur->general_tag);
        cur->ticks = cur->priority;
        cur->status = TASK_READY;
    } else {  // 其他情况, 比如等待磁盘io, 就不能加到就绪队列
    }

    if (list_empty(&thread_ready_list)) { thread_unblock(idel_thread); }
    thread_tag = NULL;
    thread_tag = list_pop(&thread_ready_list);
    // elem2entry把tag转化成task_struct
    // 第三个参数是thread_tag, 就是要转化的tag, 第一个参数是要转化成什么类
    // 第二个参数是thread_tag在这个类里面叫什么
    struct task_struct* next =
        elem2entry(struct task_struct, general_tag, thread_tag);
    next->status = TASK_RUNNING;

    // 激活页目录表和页表
    process_activate(next);
    switch_to(cur, next);
}

// 主动让出cpu
void thread_yield() {
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
    list_append(&thread_ready_list, &cur->general_tag);
    cur->status = TASK_READY;
    schedule();
    intr_set_status(old_status);
}

// 当前线程将自己阻塞, 并把自己状态设置为stat
void thread_block(enum task_status stat) {
    ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) ||
            (stat == TASK_HANGING)));
    enum intr_status old_status = intr_disable();
    struct task_struct* cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();
    intr_set_status(old_status);
}

void thread_unblock(struct task_struct* pthread) {
    enum intr_status old_status = intr_disable();
    ASSERT(((pthread->status == TASK_BLOCKED) ||
            (pthread->status == TASK_WAITING) ||
            (pthread->status == TASK_HANGING)));
    if (pthread->status != TASK_READY) {
        ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
        if (elem_find(&thread_ready_list, &pthread->general_tag)) {
            PANIC("thread_unblock: blocked thread in ready_list\n");
        }
        list_push(&thread_ready_list, &pthread->general_tag);
        pthread->status = TASK_READY;
    }
    intr_set_status(old_status);
}

void thread_init() {
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    make_main_thread();  // 将main函数创建为线程
    idel_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}
