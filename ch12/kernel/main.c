#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall.h"
#include "syscall-init.h"
#include "stdio.h"
#include "memory.h"

void k_thread_a(void*);
void k_thread_b(void*);
void u_prog_a(void);
void u_prog_b(void);
int prog_a_pid = 0, prog_b_pid = 0;

void main() {
    put_str("I am kernel\n");
    init_all();

    intr_enable();
    thread_start("k_thread_a", 31, k_thread_a, "argA ");
    thread_start("k_thread_b", 31, k_thread_b, "argB ");
    while (1);
}

void k_thread_a(void* arg) {
    char* para = arg;
    void* addr = sys_malloc(33);
    console_put_str(" im thread_a, sys_malloc(33), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    while (1);
}

void k_thread_b(void* arg) {
    char* para = arg;
    void* addr = sys_malloc(55);
    console_put_str(" im thread_b, sys_malloc(55), addr is 0x");
    console_put_int((int)addr);
    console_put_char('\n');
    while (1);
}
