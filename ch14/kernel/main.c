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
    int a = sys_unlink("/file1");
    printf("delete file: %d", a);
    while(1);
}
