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

#define EFLAGS_IF   0x00000200
#define GET_EFLAGS(EFLAG_VAR) asm volatile ("pushfl; popl %0" : "=g" (EFLAG_VAR))


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

    set_cursor(0);
    int cursor_pos = 0;
    while (cursor_pos < 320) {
        put_char(' ');
        cursor_pos++;
    }
    set_cursor(0);
    put_str("!!!!!!!      excetion message begin  !!!!!!!!\n");
    set_cursor(88);
    put_str(intr_name[vec_nr]);
    if (vec_nr == 14) { // page fault
        int page_fault_vaddr = 0;
        asm ("movl %%cr2, %0" : "=r" (page_fault_vaddr));	  // cr2是存放造成page_fault的地址
        put_str("\npage fault addr is "); put_int(page_fault_vaddr); 
    }
    put_str("\n!!!!!!!      excetion message end    !!!!!!!!\n");
    while (1);
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

// 开中断, 返回值是之前的状态
enum intr_status intr_enable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        return old_status;
    } else {
        old_status = INTR_OFF;
        asm volatile("sti"); // 开中断指令
        return old_status;
    }
}

// 关中断, 返回值是之前的状态
enum intr_status intr_disable() {
    enum intr_status old_status;
    if (INTR_ON == intr_get_status()) {
        old_status = INTR_ON;
        asm volatile("cli" : : : "memory"); // 关中断指令
        return old_status;
    } else {
        old_status = INTR_OFF;
        return old_status;
    }
}

enum intr_status intr_set_status(enum intr_status status) {
    return status & INTR_ON ? intr_enable() : intr_disable();
}

enum intr_status intr_get_status() {
    uint32_t eflags = 0;
    GET_EFLAGS(eflags);
    return (EFLAGS_IF & eflags) ? INTR_ON : INTR_OFF;
}

void register_handler(uint8_t vector_no, intr_handler function) {
    idt_table[vector_no] = function;
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
