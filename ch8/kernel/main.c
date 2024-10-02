#include "print.h"
#include "init.h"
#include "debug.h"

void main() {
    put_str("I am kernel\n");
    init_all();
    // asm volatile ("sti"); // set interrupt flag 打开中断
    ASSERT(1 == 2);
    while (1);
}