#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"    
#include "list.h"    
#include "tss.h"    
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(); // kernel.S中定义的函数

void start_process(void* filename_) {
    // pcb占一个page, self_kstack在init_thread函数中, 一开始置于page最上面:
    // pthread->self_kstack = (uint32_t*) pthread + PG_SIZE
    // 然后再预留 intr_stack 和 thread_stack 的空间:
    // pthread->self_kstack -= sizeof(struct intr_stack);
    // pthread->self_kstack -= sizeof(struct thread_stack);
    // intr_stack存储进入中断时的上下文
    // thread_stack存储中断处理时, 以及switch_to前后的上下文
    void* function = filename_;
    struct task_struct* cur = running_thread(); // 当前thread还是系统线程
    cur->self_kstack += sizeof(struct thread_stack); // 跳过 kernel stack, 得到intr stack的地址
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
    proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
    proc_stack->gs = 0; // 用户态不需要
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    proc_stack->eip = function; // 待执行的用户程序地址
    proc_stack->cs = SELECTOR_U_CODE;
    // eflags: MBS必须为1, IOPL0 IO端口控制, IF 1开中断
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE); // stack pointer
    proc_stack->ss = SELECTOR_U_DATA;
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

// 激活页表
void page_dir_activate(struct task_struct* p_thread) {
    // 执行这个函数的可能是用户进程也可能是系统线程. 都需要重新安装页表
    uint32_t pagedir_phy_addr = 0x100000; // 默认是内核进程, 页目录地址为0x100000;
    if (p_thread->pgdir != NULL) { // 有自己页目录表和页表的是用户进程
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }
    asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

// 激活线程或进程的页表, 更新tss中的esp0为进程的特权级为0的栈
void process_activate(struct task_struct* p_thread) {
    ASSERT(p_thread != NULL);
    page_dir_activate(p_thread);

    if (p_thread->pgdir) {
        update_tss_esp(p_thread);
    }
}

// 创建页目录表, 返回页目录的虚拟地址, 失败返回-1
uint32_t* create_page_dir() {
    uint32_t* page_dir_vaddr = get_kernel_pages(1);
    if (page_dir_vaddr == NULL) {
        console_put_char("create_page_dir: get_kernel_page failed!");
        return NULL;
    }

    // 内核空间1G, 被所有的用户进程共享
    // 复制页表, page_dir_vaddr + 0x300 * 4是内核页目录的第768项
    // 用户进程在内核中创建, 此时的页表还是内核的页表. 0xfffff000是页目录项地址, 加上0x300 * 4
    // 就可以访问到第768项, 后面256项, 每项4B, 共1024B.
    memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t*)(0xfffff000 + 0x300 * 4), 1024);

    // 更新页目录地址.
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    // 最后一个页目录项记录用户进程自己的页目录表的物理地址.
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
    return page_dir_vaddr;
}

// 创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE); // bitmap所需要的page个数
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt); // 从内核空间申请page, 用来存储bitmap
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

// 创建用户进程
void process_execute(void* filename, char* name) {
    struct task_struct* thread = get_kernel_pages(1); // pcb占一个page
    init_thread(thread, name, default_prio);
    create_user_vaddr_bitmap(thread);
    thread_create(thread, start_process, filename);
    thread->pgdir = create_page_dir();


    // 添加到内核的 thread list, 包括 ready list 和 all list
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
    list_append(&thread_ready_list, &thread->general_tag);

    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}
