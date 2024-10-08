#include "print.h"
#include "init.h"
#include "thread.h"

void k_thread_a(void*);

void main() {
    put_str("I am kernel\n");
    init_all();

    thread_start("k_thread_a", 31, k_thread_a, "argA ");
    
    while (1);
}

void k_thread_a(void* arg) {
    char* para = arg;
    while (1) {
        put_str(para);
    }
}
