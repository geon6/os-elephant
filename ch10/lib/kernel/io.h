#ifndef __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"

// 向port写入一个字节
static inline void outb(uint16_t port, uint8_t data) {
    // volatile保证这段内联汇编不会被优化
    // 第一个:后面是输出约束, 这里没有输出约束
    // 第二个:后面是输入约束, a表示al或者eax, d表示dx, N表示不需要保存dx的内容
    // 执行命令里面, %b0表示第一个参数, b是取一个byte, %w1表示第二个参数, w是取一个word(2字节)
    asm volatile("outb %b0, %w1" : : "a" (data), "Nd" (port));
}

// 从addr读入word_cnt个字节, 并写入port
static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt) {
    // cld: clear DF(direction flag)
    // rep outsw:重复执行outsw直到寄存器cx变为0
    // +表示这个约束是输入约束也是输出约束, 变量在执行过程会被更新
    // "+S" (addr) 输出约束: addr放在ESI
    // "+c" (word_cnt) 输出约束: word_cnt放在ECX
    // "d" (port) 输入约束: 把输入port放在寄存器dx
    asm volatile ("cld; rep outsw" : "+S" (addr), "+c" (word_cnt) : "d" (port));
}

// 从port读入一个字节
static inline uint8_t inb(uint16_t port) {
    uint8_t data;
    asm volatile ("inb %w1, %b0" : "=a" (data) : "Nd" (port));
    return data;
}

// 从port读入word_cnt个字节, 并写入addr
static inline void insw(uint16_t port, void* addr, uint32_t word_cnt) {
    asm volatile ("cld; rep insw" : "+D" (addr), "+c" (word_cnt) : "d" (port) : "memory");
}

#endif