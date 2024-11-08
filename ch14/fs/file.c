#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

struct file file_table[MAX_FILE_OPEN];

// 从file_table中获取一个空闲位, 成功返回下标, 失败返回-1
int32_t get_free_slot_in_global() {
    uint32_t fd_idx = 3;
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            break;
        }
        fd_idx++;
    }
    if (fd_idx == MAX_FILE_OPEN) {
        printk("exceed max open files\n");
        return -1;
    }
    return fd_idx;
}

// 将全局描述符下标安装到线程自己的文件描述符数组中, 成功返回下标, 失败返回-1
int32_t pcb_fd_install(int32_t global_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] = global_fd_idx;
            break;
        }
    }
    if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
        printk("exceed max open files_per_proc\n");
        return -1;
    }
    return local_fd_idx;
}

// 申请一个inode, 并把对应bitmap置1, 返回bitmap位的下标
int32_t inode_bitmap_alloc(struct partition* part) {
    // 获得一个free的bitmap下标
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

// 分配一个扇区, 并返回扇区地址 (而不是bitmap下标)
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    return (part->sb->data_start_lba + bit_idx);
}

// 将内存中的bitmap第bit_idx位所在的512字节(一个扇区)sync到硬盘上
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type) {
    uint32_t off_sec = bit_idx / 4096; // 以扇区为单位的offset
    uint32_t off_size = off_sec * BLOCK_SIZE; // 以字节为单位
    uint32_t sec_lba; // sec_lba表示要写到硬盘哪个地方
    uint8_t* bitmap_off; // bitmap_off指向内存中需要写入的那个块

    switch (btmp_type) {
        case INODE_BITMAP:
            sec_lba = part->sb->inode_bitmap_lba + off_sec;
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;
        case BLOCK_BITMAP:
            sec_lba = part->sb->block_bitmap_lba + off_sec;
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

// 只用于file_create中, 创建文件失败时, 释放各种资源
static int32_t rollback(uint8_t rollback_step, void* io_buf, int32_t inode_no, struct inode* new_file_inode, int32_t fd_idx) {
    switch (rollback_step) {
        case 3:
            memset(&file_table[fd_idx], 0, sizeof(struct file));
        case 2:
            sys_free(new_file_inode);
        case 1:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    sys_free(io_buf);
    return -1;
}

// 创建文件, 指定filename, 父目录为parent_dir, flag可以控制是否覆盖等操作
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
    // 子函数经常需要一块内存来存inode 或者dir等数据结构, 这里分配一次, 避免多次分配回收
    void* io_buf = sys_malloc(1024);
    if (io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed\n");
        return -1;
    }

    uint8_t rollback_step = 0; // 用于操作失败时进行回滚

    // 给新文件分配inode
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_create: allocate inode failed\n");
        return -1;
    }
    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
    if (new_file_inode == NULL) {
        printk("file_create: sys_malloc for inode failed\n");
        rollback_step = 1;
        return rollback(rollback_step, io_buf, inode_no, NULL, 0);
    }
    inode_init(inode_no, new_file_inode);

    // 返回file_table的下标
    int32_t fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        return rollback(rollback_step, io_buf, inode_no, new_file_inode, 0);
    }
    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    file_table[fd_idx].fd_inode->write_deny = false;

    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));

    // 把新创的dir_entry存到父目录里
    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        return rollback(rollback_step, io_buf, inode_no, new_file_inode, fd_idx);
    }

    // inode写到硬盘, 需要写parent的inode, 也需要写新创文件的inode
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, parent_dir->inode, io_buf);
    memset(io_buf, 0, 1024);
    inode_sync(cur_part, new_file_inode, io_buf);

    // inode bitmap同步到硬盘
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    // 新创文件添加到open_inodes链表
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);
    return pcb_fd_install(fd_idx);
}

// 打开inode_no的文件, 成功返回fd, 失败返回-1
int32_t file_open(uint32_t inode_no, uint8_t flag) {
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }

    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos = 0;
    file_table[fd_idx].fd_flag = flag;
    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

    if (flag & O_WRONLY || flag & O_RDWR) {
        // 写文件需要判断有没有其他线程在写文件
        enum intr_status old_status = intr_disable();
        if (!(*write_deny)) {
            *write_deny = true;
            intr_set_status(old_status);
        } else {
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);
}

int32_t file_close(struct file* file) {
    if (file == NULL) {
        return -1;
    }
    file->fd_inode->write_deny = false;
    inode_close(file->fd_inode);
    file->fd_inode = NULL;
    return 0;
}
