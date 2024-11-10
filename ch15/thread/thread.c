#include "thread.h"
#include "debug.h"
#include "file.h"
#include "fs.h"
#include "global.h"
#include "interrupt.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "sync.h"

struct task_struct* idle_thread;      // idle线程
struct task_struct* main_thread;      // 主线程PCB
struct lock pid_lock;                 // pid锁, 用于分配pid
struct list thread_ready_list;        // 就绪队列
struct list thread_all_list;          // 所有任务队列
static struct list_elem* thread_tag;  // 用于保存队列中的线程结点

extern void switch_to(struct task_struct* cur, struct task_struct* next);

extern void init();

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

/* fork进程时为其分配pid,因为allocate_pid已经是静态的,别的文件无法调用.
不想改变函数定义了,故定义fork_pid函数来封装一下。*/
pid_t fork_pid() {
    return allocate_pid();
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

    if (list_empty(&thread_ready_list)) { thread_unblock(idle_thread); }
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

/* 以填充空格的方式输出buf */
static void pad_print(char* buf, int32_t buf_len, void* ptr, char format) {
    memset(buf, 0, buf_len);
    uint8_t out_pad_0idx = 0;
    switch (format) {
        case 's': out_pad_0idx = sprintf(buf, "%s", ptr); break;
        case 'd': out_pad_0idx = sprintf(buf, "%d", *((int16_t*)ptr));
        case 'x': out_pad_0idx = sprintf(buf, "%x", *((uint32_t*)ptr));
    }
    while (out_pad_0idx < buf_len) {  // 以空格填充
        buf[out_pad_0idx] = ' ';
        out_pad_0idx++;
    }
    sys_write(stdout_no, buf, buf_len - 1);
}

/* 用于在list_traversal函数中的回调函数,用于针对线程队列的处理 */
static bool elem2thread_info(struct list_elem* pelem, int arg UNUSED) {
    struct task_struct* pthread =
        elem2entry(struct task_struct, all_list_tag, pelem);
    char out_pad[16] = {0};

    pad_print(out_pad, 16, &pthread->pid, 'd');

    if (pthread->parent_pid == -1) {
        pad_print(out_pad, 16, "NULL", 's');
    } else {
        pad_print(out_pad, 16, &pthread->parent_pid, 'd');
    }

    switch (pthread->status) {
        case 0: pad_print(out_pad, 16, "RUNNING", 's'); break;
        case 1: pad_print(out_pad, 16, "READY", 's'); break;
        case 2: pad_print(out_pad, 16, "BLOCKED", 's'); break;
        case 3: pad_print(out_pad, 16, "WAITING", 's'); break;
        case 4: pad_print(out_pad, 16, "HANGING", 's'); break;
        case 5: pad_print(out_pad, 16, "DIED", 's');
    }
    pad_print(out_pad, 16, &pthread->elapsed_ticks, 'x');

    memset(out_pad, 0, 16);
    ASSERT(strlen(pthread->name) < 17);
    memcpy(out_pad, pthread->name, strlen(pthread->name));
    strcat(out_pad, "\n");
    sys_write(stdout_no, out_pad, strlen(out_pad));
    return false;  
    // 此处返回false是为了迎合主调函数list_traversal,只有回调函数返回false时才会继续调用此函数
}

/* 打印任务列表 */
void sys_ps() {
    char* ps_title =
        "PID            PPID           STAT           TICKS          COMMAND\n";
    sys_write(stdout_no, ps_title, strlen(ps_title));
    list_traversal(&thread_all_list, elem2thread_info, 0);
}

void thread_init() {
    put_str("thread_init start\n");
    list_init(&thread_ready_list);
    list_init(&thread_all_list);
    lock_init(&pid_lock);
    process_execute(init, "init");
    make_main_thread();  // 将main函数创建为线程
    idle_thread = thread_start("idle", 10, idle, NULL);
    put_str("thread_init done\n");
}
