#include "print.h"
#include "init.h"

void main() {
    put_str("I am kernel\n");
    init_all();
    asm volatile ("sti"); // set interrupt flag 打开中断
    while (1);
}