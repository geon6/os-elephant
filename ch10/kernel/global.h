#ifndef __KERNEL_GLOBAL_H
#define __KERNEL_GLOBAL_H

#include "stdint.h"

// Requested Privilege Level, 选择子需要特权等级
#define RPL0 0
#define RPL1 1
#define RPL2 2
#define RPL3 3

// 选择子中有一个位表示选择子用于GDT还是LDT
#define TI_GDT 0
#define TI_LDT 1

// 选择子的另外13位表示index, 用来从GDT or LDT中进行索引
#define SELECTOR_K_CODE     ((1 << 3) + (TI_GDT << 2) + RPL0)
#define SELECTOR_K_DATA     ((2 << 3) + (TI_GDT << 2) + RPL0)
#define SELECTOR_K_STACK    SELECTOR_K_DATA
#define SELECTOR_K_GS       ((3 << 3) + (TI_GDT << 2) + RPL0)

#define IDT_DESC_P              1
#define IDT_DESC_DPL0           0
#define IDT_DESC_DPL3           3
#define IDT_DESC_32_TYPE        0xE
#define IDT_DESC_16_TYPE        0x6
#define IDT_DESC_ATTR_DPL0      ((IDT_DESC_P << 7) + (IDT_DESC_DPL0 << 5) + IDT_DESC_32_TYPE)
#define IDT_DESC_ATTR_DPL3      ((IDT_DESC_P << 7) + (IDT_DESC_DPL3 << 5) + IDT_DESC_32_TYPE)

#define NULL ((void*)0)
#define bool int
#define true 1
#define false 0

#endif
