#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include "global.h"

#define BITMAP_MASK 1

struct bitmap {
    uint32_t btmp_bytes_len;
    uint8_t* bits;
};

// bitmap初始化
void bitmap_init(struct bitmap* btmp);

// 判断bit_idx是否为1
bool bitmap_scan_test(struct bitmap* btmp, uint32_t bit_idx);

// 申请cnt个位, 失败返回-1, 成功返回起始位下标
int bitmap_scan(struct bitmap* btmp, uint32_t cnt);

// bit_idx位设置为value
void bitmap_set(struct bitmap* btmp, uint32_t bit_idx, int8_t value);

#endif