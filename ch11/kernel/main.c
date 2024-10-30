#include "print.h"
#include "init.h"
#include "thread.h"
#include "console.h"
#include "ioqueue.h"
#include "keyboard.h"
#include "interrupt.h"

void k_thread_a(void*);
void k_thread_b(void*);

void main() {
    put_str("I am kernel\n");
    init_all();

    thread_start("k_thread_a", 31, k_thread_a, " A_");
    thread_start("k_thread_b", 31, k_thread_b, " B_");
    
    intr_enable(); // 打开中断, 时钟中断促使切换线程
    while (1);
    // int i = 999999;
    // while (1) {
    //     while (i--);
    //     i = 999999;
    //     console_put_str("main ");
    // }
}

void k_thread_a(void* arg) {
    char* para = arg;
    int i = 999999;
    while (1) {
        enum intr_status old_status = intr_disable();
        while (i--);
        i = 999999;
        if (!ioq_empty(&kbd_buf)) {
            console_put_str(arg);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    int i = 999999;
    while (1) {
        enum intr_status old_status = intr_disable();
        while (i--);
        i = 999999;
        if (!ioq_empty(&kbd_buf)) {
            console_put_str(arg);
            char byte = ioq_getchar(&kbd_buf);
            console_put_char(byte);
        }
        intr_set_status(old_status);
    }
}
