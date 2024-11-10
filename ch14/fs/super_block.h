#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

#include "stdint.h"

// 每个分区有自己的超级块
struct super_block {
    uint32_t magic;               // 用来表示文件系统的类型
    uint32_t sec_cnt;             // 本分区中的扇区数
    uint32_t inode_cnt;           // 本分区的inode数
    uint32_t part_lba_base;       // 本分区的lba地址
    uint32_t block_bitmap_lba;    // 块位图的lba
    uint32_t block_bitmap_sects;  // 块位图占多少扇区
    uint32_t inode_bitmap_lba;    // inode位图的lba
    uint32_t inode_bitmap_sects;  // inode位图占多少扇区
    uint32_t inode_table_lba;     // inode表的lba
    uint32_t inode_table_sects;   // inode表占多少扇区
    uint32_t data_start_lba;      // 数据区开始的lba
    uint32_t root_inode_no;       // root dir的inode no
    uint32_t dir_entry_size;      // 目录项大小
    uint8_t pad[460];             // 凑够512B, 一个扇区
} __attribute__((packed));

#endif