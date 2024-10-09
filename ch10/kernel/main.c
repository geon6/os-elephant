#include "print.h"
#include "init.h"
#include "thread.h"


void k_thread_a(void*);
void k_thread_b(void*);

void main() {
    put_str("I am kernel\n");
    init_all();

    thread_start("k_thread_a", 31, k_thread_a, "argA ");
    thread_start("k_thread_b", 8, k_thread_b, "argB ");
    
    intr_enable(); // 打开中断, 时钟中断促使切换线程
    while (1) {
        intr_disable();
        put_str("main ");
        intr_enable();
    }
}

void k_thread_a(void* arg) {
    char* para = arg;
    while (1) {
        intr_disable();
        put_str(para);
        intr_enable();
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    while (1) {
        intr_disable();
        put_str(para);
        intr_enable();
    }
}
