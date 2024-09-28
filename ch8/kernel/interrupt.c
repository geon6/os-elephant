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

char* intr_name[IDT_DESC_CNT];
intr_handler idt_table[IDT_DESC_CNT];
extern intr_handler intr_entry_table[IDT_DESC_CNT];

static void pic_init() {
    // 初始化主片
    outb (PIC_M_CTRL, 0x11);    // 初始化ICW1, 0x11的意义参考图7-13
    outb (PIC_M_DATA, 0x20);    // 初始化ICW2, IRQ0的中断向量映射到0x20, IRQ1...依次递增
    outb (PIC_M_DATA, 0x04);    // 初始化ICW3, ICW3用来控制主片和从片用哪个引脚相连. 这里选择IRQ2
    outb (PIC_M_DATA, 0x01);

    // 初始化从片
    outb (PIC_S_CTRL, 0x11);
    outb (PIC_S_DATA, 0x28);
    outb (PIC_S_DATA, 0x02);
    outb (PIC_S_DATA, 0x01);

    // 只打开主片上的IRQ0, 也就是时钟中断. 参考图7-11
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

// 通用的中断处理函数
static void general_intr_handler(uint8_t vec_nr) {
    // 8259A主片中断号的范围是 0x20 - 0x27
    // 从片的中断号范围是 0x28 - 0x2f
    // 这两个中断保留
    if (vec_nr == 0x27 || vec_nr == 0x2f) {
        return ;
    }
    put_str("int vector : 0x");
    put_int(vec_nr);
    put_char('\n');
}

static void exception_init() {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        idt_table[i] = general_intr_handler;
        intr_name[i] = "unknown";
    }
    intr_name[0] = "#DE Divide Error";
    intr_name[1] = "#DB Debug Exception";
    intr_name[2] = "NMI Interrupt";
    intr_name[3] = "#BP Breakpoint Exception";
    intr_name[4] = "#OF Overflow Exception";
    intr_name[5] = "#BR BOUND Range Exceeded Exception";
    intr_name[6] = "#UD Invalid Opcode Exception";
    intr_name[7] = "#NM Device Not Available Exception";
    intr_name[8] = "#DF Double Fault Exception";
    intr_name[9] = "Coprocessor Segment Overrun";
    intr_name[10] = "#TS Invalid TSS Exception";
    intr_name[11] = "#NP Segment Not Present";
    intr_name[12] = "#SS Stack Fault Exception";
    intr_name[13] = "#GP General Protection Exception";
    intr_name[14] = "#PF Page-Fault Exception";
    // intr_name[15] 第15项是intel保留项，未使用
    intr_name[16] = "#MF x87 FPU Floating-Point Error";
    intr_name[17] = "#AC Alignment Check Exception";
    intr_name[18] = "#MC Machine-Check Exception";
    intr_name[19] = "#XF SIMD Floating-Point Exception";
}

// interrupt descriptor table init
void idt_init() {
    put_str("idt_init start\n");
    idt_desc_init();
    exception_init();
    pic_init();

    // 加载idt
    uint64_t idt_operand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m" (idt_operand));
    put_str("idt_init done\n");
}
