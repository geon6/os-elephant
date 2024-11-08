#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "fs.h"

int main(void) {
    put_str("i am kernel\n");
    init_all();
    sys_open("/file2", O_CREAT);
    while(1);
}
