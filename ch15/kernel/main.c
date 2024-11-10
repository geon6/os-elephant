#include "console.h"
#include "dir.h"
#include "fs.h"
#include "init.h"
#include "interrupt.h"
#include "memory.h"
#include "print.h"
#include "process.h"
#include "shell.h"
#include "stdio.h"
#include "syscall-init.h"
#include "syscall.h"
#include "thread.h"

void init();

int main(void) {
    put_str("I am kernel\n");
    init_all();

    uint32_t file_size = 4777;

    cls_screen();
    console_put_str("[geon6@localhost /]$ ");
    while (1) {}
    return 0;
}

void init() {
    uint32_t ret_pid = fork();
    if (ret_pid) {
        while (1) {}
    } else {
        my_shell();
    }
    while (1) {}
}
