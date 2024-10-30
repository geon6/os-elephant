#include "keyboard.h"
#include "print.h"
#include "io.h"
#include "interrupt.h"
#include "global.h"

#define KBD_BUF_PORT 0x60   // 键盘buffer寄存器端口号

static void intr_keyboard_handler() {
    put_char('k');
    uint8_t scancode = inb(KBD_BUF_PORT); // 不读出缓冲区寄存器的话, 8042就不会再响应键盘中断
    return ;
}

void keyboard_init() {
    put_str("keyboard init start\n");
    register_handler(0x21, intr_keyboard_handler);
    put_str("keyboard init done\n");
}
