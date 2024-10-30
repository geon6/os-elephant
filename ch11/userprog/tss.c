#include "tss.h"
#include "stdint.h"
#include "global.h"
#include "string.h"
#include "print.h"

// 参考图11-4. 
// TSS里面基本都是寄存器, 表示他们的状态
// 使用 TSS 唯一的理由是为 0 特权级的任务提供栈
// 具体, 切换进程时, 把ss0和esp0更新为对应的内核栈的段地址及栈指针
// 为什么不完全用TSS?
// 1. 效率低下  2. TSS需要在GDT中注册, GDT最多有8192个项, 我们需要更多的进程数
// linux中pid默认最大为32768, 还可以修改为更大的
struct tss {
    uint32_t backlink;
    uint32_t* esp0;
    uint32_t ss0;
    uint32_t* esp1;
    uint32_t ss1;
    uint32_t* esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t (*eip) (void);
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint32_t trace;
    uint32_t io_base;
};

static struct tss tss;

// 将tss中esp0修改为参数pthread的0级栈地址
void update_tss_esp(struct task_struct* pthread) {
    // pthread指向pcb, pcb再往上一页就是内核栈
    tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);
}

static struct gdt_desc make_gdt_desc(uint32_t* desc_addr, uint32_t limit, uint8_t attr_low, uint8_t attr_high) {
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;
    desc.limit_low_word = limit & 0x0000ffff;
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.attr_low_byte = (uint8_t)(attr_low);
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)(attr_high));
    desc.base_high_byte = desc_base >> 24;
    return desc;
}

// 再gdt中创建tss, 并重新加载gdt
void tss_init() {
    put_str("tss_init start\n");
    uint32_t tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);
    tss.ss0 = SELECTOR_K_STACK;
    tss.io_base = tss_size;

    // gdt段基址为0x900, 每个gdt desc有64bit
    *((struct gdt_desc*)0xc0000920) = make_gdt_desc(
        (uint32_t*)&tss,
        tss_size - 1,
        TSS_ATTR_LOW,
        TSS_ATTR_HIGH
    );
    *((struct gdt_desc*)0xc0000928) = make_gdt_desc(
        (uint32_t*)0,
        0xfffff,
        GDT_CODE_ATTR_LOW_DPL3,
        GDT_ATTR_HIGH
    );
    *((struct gdt_desc*)0xc0000930) = make_gdt_desc(
        (uint32_t*)0, 
        0xfffff,
        GDT_DATA_ATTR_LOW_DPL3,
        GDT_ATTR_HIGH
    );
    uint64_t gdt_operand = ((8 * 7 - 1) | ((uint64_t)(uint32_t)0xc0000900) << 16);
    asm volatile ("lgdt %0" : : "m" (gdt_operand));
    asm volatile ("ltr %w0" : : "r" (SELECTOR_TSS));
    put_str("tss_init and ltr done\n");
}
