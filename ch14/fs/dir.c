#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir; // 根目录

// 打开根目录
void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

// 打开分区part上inode_no号节点
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

// 在part分区的pdir目录里, 查找名字为name的文件or目录
// 找到后返回true, 并把目录项存到dir_e. 否则返回false
bool search_dir_entry(struct partition* part, struct dir* pdir, const char* name, struct dir_entry* dir_e) {
    uint32_t block_cnt = 140; // 12个直接块, 一个一级间接块(128), 加起来140块
    uint32_t* all_blocks = (uint32_t*)sys_malloc(560); // 每个块的地址4字节, 4 * 140 = 560
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed");
        return false;
    }
    uint32_t block_idx = 0;
    while (block_idx < 12) {
        // inode->i_sectors存放的是地址
        all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
        block_idx++;
    }
    block_idx = 0;

    // 如果有间接块
    if (pdir->inode->i_sectors[12] != 0) {
        ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
    }
    
    // 读目录项
    uint8_t* buf = (uint8_t*)sys_malloc(SECTOR_SIZE);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size; // 每个扇区中的目录项个数

    // 在所有块中, 查找对应名字的目录项
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, all_blocks[block_idx], buf, 1);
        uint32_t dir_entry_idx = 0;
        // 遍历块中的所有目录项
        while (dir_entry_idx < dir_entry_cnt) {
            if (!strcmp(p_de->filename, name)) {
                // 找到了
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        p_de = (struct dir_entry*)buf;
        memset(buf, 0, SECTOR_SIZE);
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

// 关闭目录
void dir_close(struct dir* dir) {
    // 根目录不能关闭
    if (dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

// 初始化目录项, 指定inode_on, file_type, filename
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, struct dir_entry* p_de) {
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

// 把目录项p_de写入父目录项parent_dir, io_buf由caller提供
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    ASSERT(dir_size % dir_entry_size == 0);
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    int32_t block_lba = -1;

    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0}; // all_blocks保存目录所有的块
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }

    struct dir_entry* dir_e = (struct dir_entry*)io_buf; // 用来遍历目录项
    int32_t block_bitmap_idx = -1;

    block_idx = 0;
    while (block_idx < 140) { // 总共140个块
        block_bitmap_idx = -1;
        if (all_blocks[block_idx] == 0) {
            block_lba = block_bitmap_alloc(cur_part);
            if (block_lba == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                return false;
            }

            // 分配一次块就同步一次block_bitmap
            block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
            ASSERT(block_bitmap_idx != -1);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
            block_bitmap_idx = -1;

            if (block_idx < 12) { // 直接块
                dir_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
            } else if (block_idx == 12) { // 第一次分配间接块
                dir_inode->i_sectors[12] = block_lba;
                block_lba = -1;
                block_lba = block_bitmap_alloc(cur_part); // 再分配一个作为间接块
                if (block_lba == 12) { // 分配失败
                    block_bitmap_idx = dir_inode->i_sectors[12] - cur_part->sb->data_start_lba;
                    bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
                    dir_inode->i_sectors[12] = 0; // 把之前申请到的block还回去
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    return false;
                }

                block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
                ASSERT(block_bitmap_idx != -1);
                bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                all_blocks[12] = block_lba;
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            } else { // 间接块
                all_blocks[block_idx] = block_lba;
                ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
            }

            memset(io_buf, 0, 512);
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            dir_inode->i_size += dir_entry_size;
            return true;
        }

        // 上面的思路: block空, 说明找不到要插入的dir, 申请一个block, 并存入dir
        // 下面的思路: block非空, 把block读出来, 看看有没有空位, 有就存入
        // 把block读出来
        ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
        // 遍历inode
        uint8_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) { // 有空位
                // 文件被删除后, 标记为unknown类型, 只经过初始化的文件类型也是unknown
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
                dir_inode->i_size += dir_entry_size;
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    return false;
}

// 将分区part目录pdir中的inode_no删除
bool delete_dir_entry(struct partition* part, struct dir* pdir, uint32_t inode_no, void* io_buf) {
    struct inode* dir_inode = pdir->inode;
    uint32_t block_idx = 0, all_blocks[140] = {0};
    while (block_idx < 12) {
        all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (dir_inode->i_sectors[12]) {
        ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
    }

    // 目录项不会跨扇区的
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size); // 每个扇区的dir数
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    struct dir_entry* dir_entry_found = NULL;
    uint8_t dir_entry_idx, dir_entry_cnt;
    bool is_dir_first_block = false;

    block_idx = 0;
    while (block_idx < 140) {
        is_dir_first_block = false;
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        dir_entry_idx = dir_entry_cnt = 0;
        memset(io_buf, 0, SECTOR_SIZE);
        ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1); // 读取扇区, 获得目录项

        // 遍历目录项
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    is_dir_first_block = true; // 标记成第一块, 第一块不删除
                } else if (strcmp((dir_e + dir_entry_idx)->filename, ".") && strcmp((dir_e + dir_entry_idx)->filename, "..")) {
                    dir_entry_cnt++; // 记录块里有多少项
                    // 如果只有一项, 并且要删除, 那么要释放这个块
                    if ((dir_e + dir_entry_idx)->i_no == inode_no) {
                        // 找到了
                        ASSERT(dir_entry_found == NULL);
                        dir_entry_found = dir_e + dir_entry_idx;
                    }
                }
            }
            dir_entry_idx++;
        }

        // 这个扇区没找到, 进入下次循环
        if (dir_entry_found == NULL) {
            block_idx++;
            continue;
        }

        ASSERT(dir_entry_cnt >= 1);
        if (dir_entry_cnt == 1 && !is_dir_first_block) { // 回收扇区
            uint32_t block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
            bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
            bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

            if (block_idx < 12) {
                // 直接块的回收(根本不用回收, 反正直接写入就行)
                // 把对应地址清除
                dir_inode->i_sectors[block_idx] = 0;
            } else {
                // 间接块的回收
                uint32_t indirect_blocks = 0;
                uint32_t indirect_block_idx = 12;
                while (indirect_block_idx < 140) {
                    if (all_blocks[indirect_block_idx] != 0) {
                        indirect_blocks++;
                    }
                }
                ASSERT(indirect_blocks >= 1); // 包括当前间接块

                // 如果间接块数目大于1, 不需要回收间接块的索引
                // 如果间接块数目刚好等于1, 还要回收间接块的索引
                if (indirect_blocks > 1) { 
                    // 间接索引表中还包括其它间接块,仅在索引表中擦除当前这个间接块地址
                    all_blocks[block_idx] = 0;
                    ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
                } else { 
                    // 间接索引表中就当前这1个间接块,直接把间接索引表所在的块回收,然后擦除间接索引表块地址
                    // 回收间接索引表所在的块
                    block_bitmap_idx = dir_inode->i_sectors[12] - part->sb->data_start_lba;
                    bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

                    // 将间接索引表地址清0
                    dir_inode->i_sectors[12] = 0;
                }
            }
        } else { 
            // 不用回收扇区, 只是删除inode
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
        }

        ASSERT(dir_inode->i_size >= dir_entry_size);
        dir_inode->i_size -= dir_entry_size;
        memset(io_buf, 0, SECTOR_SIZE * 2);
        inode_sync(part, dir_inode, io_buf);

        return true;
    }
    return false; // 所有块没找到inode_no, 返回false
}
