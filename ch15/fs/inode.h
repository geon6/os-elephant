#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "list.h"
#include "ide.h"

struct inode {
    uint32_t i_no;
    uint32_t i_size; // 当inode是文件时, size指文件大小; 当inode是目录时, size指目录下所有文件大小之和
    uint32_t i_open_cnts; // 此文件被打开次数
    bool write_deny; // 写文件不能并行, 写文件前要检查这个标志
    uint32_t i_sectors[13]; // 0-11是直接块, 12是一级间接块
    struct list_elem inode_tag;
};

void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_close(struct inode* inode);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);
void inode_release(struct partition* part, uint32_t inode_no);

#endif
