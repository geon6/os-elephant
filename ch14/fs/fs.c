#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"

struct partition* cur_part;

// 在分区链表中找到名为part_name的分区, 并将其指针赋给cur_part
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;
        // sb_buf用来存储从硬盘读下来的超级块
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
        // 在内存中创建cur_part的超级块
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }

        // 读入超级块
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

        // 把sb_buf中的超级块存到cur_part->sb中
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE); // 申请当前分区的block bitmap空间
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE; // block bitmap大小
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects); // 读入block bitmap

        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE); // 申请当前分区的inode bitmap空间
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE; // inode bitmap大小
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects); // 读入inode bitmap

        list_init(&cur_part->open_inodes);

        printk("mount %s done!\n", part->name);
        return true;
    }
    return false; // 返回值只是帮助list_traversal执行, 没有其他作用
}

// 格式化分区, 也就是初始化分区的元信息, 创建文件系统
static void partition_format(struct partition* part) {
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects = DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
    uint32_t free_sects = part->sec_cnt - used_sects;
    uint32_t block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
    uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
    block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

    // 超级块
    struct super_block sb;
    sb.magic = 0x19590318;
    sb.sec_cnt = part->sec_cnt;
    sb.inode_cnt = MAX_FILES_PER_PART;
    sb.part_lba_base = part->start_lba;

    sb.block_bitmap_lba = sb.part_lba_base + 2; // 第0是引导块, 第1是超级块
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);
    printk("%s info:\n", part->name);
    printk("    magic:0x:%x\n    part_lba_base:0x%x\n    all_sectors:0x%x\n    inode_bitmap_lba:0x%x\n    inode_bitmap_sectors:0x%x\n    inode_table_lba:0x%x\n    inode_table_sectors:0x%x\n    data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

    struct disk* hd = part->my_disk;
    // 把超级块写入1扇区
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba:0x%x\n", part->start_lba + 1);
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ? sb.block_bitmap_sects : sb.inode_bitmap_sects);
    buf_size = (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) * SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);

    // 把块位图初始化并写入sb.block_bitmap_lba
    buf[0] |= 0x01;
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE); // last_size是指位图最后一个扇区中, 不足一个扇区的其余部分
    // memset按字节置1, 但是最后一个字节不一定全是1, 还得特殊处理最后一字节
    memset(&buf[block_bitmap_last_byte], 0xff, last_size);

    // 最后一字节
    uint8_t bit_idx = 0;
    while (bit_idx <= block_bitmap_last_bit) {
        buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
    }
    ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

    // 将inode位图初始化, 并写入sb.inode_bitmap_lba
    memset(buf, 0, buf_size);
    buf[0] |= 0x1; // 第0个inode给根目录

    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    // 将inode数组初始化, 并写入sb.inode_table_lba
    memset(buf, 0, buf_size);
    struct inode* i = (struct inode*) buf;
    i->i_size = sb.dir_entry_size * 2;
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start_lba;
    ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

    // 将根目录写入
    memset(buf, 0, buf_size);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    // 初始化根目录的父目录
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = 0; // 根目录的父目录是自己
    p_de->f_type = FT_DIRECTORY;
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("    root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

// 例如: 输入/usr/share/xxx, 把usr parse出来, 存到name_store里面
static char* path_parse(char* pathname, char* name_store) {
    if (pathname[0] == '/') { // 跳过根目录
        while (*pathname == '/') { //  ///usr/  前面很多个/都要跳过
            pathname++;
        }
    }
    while (*pathname != '/' && *pathname != 0) {
        *name_store = *pathname;
        name_store++;
        pathname++;
    }
    if (pathname[0] == 0) {
        return NULL;
    }
    return pathname;
}

// 例如 /path, 返回1, /path/wuhu 返回2
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;
    p = path_parse(p, name);
    while (name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p) { // p不为NULL, 就继续分析路径
            p = path_parse(p, name);
        }
    }
    return depth;
}

// 搜索到file, 就返回inode no, 否则返回-1
static int search_file(const char* pathname, struct path_search_record* searched_record) {
    // 如果搜索的是根目录, 直接返回根目录的信息
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0; // 搜索路径置空
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;

    sub_path = path_parse(sub_path, name);
    while (name[0]) {
        ASSERT(strlen(searched_record->searched_path) < 512);
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);

        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            if (sub_path) {
                sub_path = path_parse(sub_path, name);
            }
            if (FT_DIRECTORY == dir_e.f_type) {
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);
                searched_record->parent_dir = parent_dir;
                continue;
            } else if (FT_REGULAR == dir_e.f_type) {
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        } else { // 找不到返回-1
            return -1;
        }
    }
    dir_close(searched_record->parent_dir);
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type = FT_DIRECTORY;
    return dir_e.i_no;
}

// 成功返回fd, 否则返回-1
int32_t sys_open(const char* pathname, uint8_t flags) {
    // 最后一个字符是/, 说明是目录, 这个函数只处理普通文件
    // 目录使用dir_open
    if (pathname[strlen(pathname) - 1] == '/') {
        printk("can't open a directory %s\n", pathname);
        return -1;
    }
    ASSERT(flags <= 7);
    int32_t fd = -1;

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    // 记录目录深度, 用于判断中间某个目录不存在的情况
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    // 检查文件是否存在
    int inode_no = search_file(pathname, &searched_record);
    bool found = (inode_no != -1 ? true : false);

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

    if (pathname_depth != path_searched_depth) { // 这两个数不一样, 说明中间有目录不存在
        printk("cannot access %s: Not a directory, subpath %s is't exist\n", pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    if (!found && !(flags & O_CREAT)) { // 没找到, 并且不是要创建文件, 返回-1
        printk("in path %s, file %s is`t exist\n", searched_record.searched_path, (strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found && (flags & O_CREAT)) { // 要创建文件, 并且文件已存在, 返回-1
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT) {
        case O_CREAT:
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/') + 1), flags);
            dir_close(searched_record.parent_dir);
            break;
        default:
            // fd = file_open(inode_no, flags);
    }
    return fd;
}

// 在磁盘上搜索文件系统, 没有文件系统的话就格式化分区, 创建文件系统
void filesys_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE); // 用来存储超级块
    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }
    printk("searching filesystem......\n");
    while (channel_no < channel_cnt) {
        dev_no = 0;
        while (dev_no < 2) {
            if (dev_no == 0) { // 跨过 hd60M.img
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while (part_idx < 12) { // 4个主分区 + 8个逻辑分区
                if (part_idx == 4) { // 开始处理逻辑分区
                    part = hd->logic_parts;
                }
                if (part->sec_cnt != 0) { // 如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);
                    // 将超级块读入
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    // 通过magic判断是什么文件系统
                    if (sb_buf->magic == 0x19590318) {
                        printk("%s has filesystem\n", part->name);
                    } else {
                        printk("formatting %s's partition %s......\n", hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }
            dev_no++;
        }
        channel_no++;
    }
    sys_free(sb_buf);

    char default_part[8] = "sdb1";
    list_traversal(&partition_list, mount_partition, (int)default_part);

    open_root_dir(cur_part);

    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx++].fd_inode = NULL;
    }
}
