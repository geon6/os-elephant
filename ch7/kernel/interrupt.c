#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "print.h"
#include "io.h"

#define PIC_M_CTRL 0x20     // PIC master control port
#define PIC_M_DATA 0x21     // PIC master data port
#define PIC_S_CTRL 0xa0     // PIC slave control port
#define PIC_S_DATA 0xa1     // PIC slave data port

#define IDT_DESC_CNT 0x21 // 共支持33个中断

// 中断门描述符 参考图7-3
struct gate_desc {
    uint16_t    func_offset_low_word;
    uint16_t    selector;
    uint8_t     dcount;
    uint8_t     attribute;
    uint16_t    func_offset_high_word;
};

static void make_idt_desc(struct gate_desc* p_gdesc,
        uint8_t attr, intr_handler function);

static struct gate_desc idt[IDT_DESC_CNT];

extern intr_handler intr_entry_table[IDT_DESC_CNT];

static void pic_init() {
    // 初始化主片
    outb (PIC_M_CTRL, 0x11);    // 初始化ICW1, 0x11的意义参考图7-13
    outb (PIC_M_DATA, 0x20);    // 初始化ICW2, IRQ0的中断向量映射到0x20, IRQ1...依次递增
    outb (PIC_M_DATA, 0x04);    // 初始化ICW3, ICW3用来控制主片和从片用哪个引脚相连. 这里选择IRQ2
    outb (PIC_M_DATA, 0x01);    // 

    // 初始化从片
    outb (PIC_S_CTRL, 0x11);
    outb (PIC_S_DATA, 0x28);
    outb (PIC_S_DATA, 0x02);
    outb (PIC_S_DATA, 0x01);

    // 只打开主片上的IRQ0
    outb (PIC_M_DATA, 0xfe);
    outb (PIC_S_DATA, 0xff);

    put_str("   pic_init done\n");
}

// 创建中断门描述符, 三个参数分别为: 描述符指针, 属性, 中断处理函数
static void make_idt_desc(struct gate_desc* p_gdesc, 
        uint8_t attr, intr_handler function) {
    p_gdesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
    p_gdesc->selector = SELECTOR_K_CODE;
    p_gdesc->dcount = 0;
    p_gdesc->attribute = attr;
    p_gdesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

// 创建所有中断门描述符
static void idt_desc_init() {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        make_idt_desc(&idt[i], IDT_DESC_ATTR_DPL0, intr_entry_table[i]);
    }
    put_str("   idt_desc_init done\n");
}

// interrupt descriptor table init
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();
    pic_init();

    // 加载idt
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m" (idt_operand));
    put_str("idt_init done\n");
}
