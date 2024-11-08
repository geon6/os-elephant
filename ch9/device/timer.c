#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"

#define IRQ0_FREQUENCY      100
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT       0x40
#define COUNTER0_NO         0
#define COUNTER_MODE        2 
#define READ_WRITE_LATCH    3
#define PIT_CONTROL_PORT    0x43 /* 0x43是控制字寄存器的位置 */

uint32_t ticks;

static void frequency_set(
    uint8_t counter_port,
    uint8_t counter_no,
    uint8_t rw1,
    uint8_t counter_mode,
    uint16_t counter_value) {
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rw1 << 4 | counter_mode << 1));
    outb(counter_port, (uint8_t)counter_value);
    outb(counter_port, (uint8_t)counter_value >> 8);
}

static void intr_timer_handler() {
    struct task_struct* cur_thread = running_thread(); // 获取当前正在运行的线程
    ASSERT(cur_thread->stack_magic == 0x19870916);

    cur_thread->elapsed_ticks++;
    ticks++;
    if (cur_thread->ticks == 0) { // 时间片用完, 调度
        schedule();
    } else {
        cur_thread->ticks--;
    }
}

// 初始化PIT8253
void timer_init() {
    put_str("timer_init start\n");
    frequency_set(
            COUNTER0_PORT,
            COUNTER0_NO,
            READ_WRITE_LATCH,
            COUNTER_MODE,
            COUNTER0_VALUE);
    
    register_handler(0x20, intr_timer_handler);
    put_str("timer_init done\n");
}

