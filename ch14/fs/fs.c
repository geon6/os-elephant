#include "fs.h"
#include "console.h"
#include "debug.h"
#include "dir.h"
#include "file.h"
#include "global.h"
#include "ide.h"
#include "inode.h"
#include "list.h"
#include "memory.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

struct partition* cur_part;

// 在分区链表中找到名为part_name的分区, 并将其指针赋给cur_part
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;
        // sb_buf用来存储从硬盘读下来的超级块
        struct super_block* sb_buf =
            (struct super_block*)sys_malloc(SECTOR_SIZE);
        // 在内存中创建cur_part的超级块
        cur_part->sb =
            (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) { PANIC("alloc memory failed!"); }

        // 读入超级块
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

        // 把sb_buf中的超级块存到cur_part->sb中
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(
            sb_buf->block_bitmap_sects *
            SECTOR_SIZE);  // 申请当前分区的block bitmap空间
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len =
            sb_buf->block_bitmap_sects * SECTOR_SIZE;  // block bitmap大小
        ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits,
                 sb_buf->block_bitmap_sects);  // 读入block bitmap

        cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(
            sb_buf->inode_bitmap_sects *
            SECTOR_SIZE);  // 申请当前分区的inode bitmap空间
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len =
            sb_buf->inode_bitmap_sects * SECTOR_SIZE;  // inode bitmap大小
        ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits,
                 sb_buf->inode_bitmap_sects);  // 读入inode bitmap

        list_init(&cur_part->open_inodes);

        printk("mount %s done!\n", part->name);
        return true;
    }
    return false;  // 返回值只是帮助list_traversal执行, 没有其他作用
}

// 格式化分区, 也就是初始化分区的元信息, 创建文件系统
static void partition_format(struct partition* part) {
    uint32_t boot_sector_sects = 1;
    uint32_t super_block_sects = 1;
    uint32_t inode_bitmap_sects =
        DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
    uint32_t inode_table_sects =
        DIV_ROUND_UP((sizeof(struct inode) * MAX_FILES_PER_PART), SECTOR_SIZE);
    uint32_t used_sects = boot_sector_sects + super_block_sects +
                          inode_bitmap_sects + inode_table_sects;
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

    sb.block_bitmap_lba = sb.part_lba_base + 2;  // 第0是引导块, 第1是超级块
    sb.block_bitmap_sects = block_bitmap_sects;

    sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
    sb.inode_bitmap_sects = inode_bitmap_sects;

    sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
    sb.inode_table_sects = inode_table_sects;

    sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
    sb.root_inode_no = 0;
    sb.dir_entry_size = sizeof(struct dir_entry);
    printk("%s info:\n", part->name);
    printk("    magic:0x:%x\n    part_lba_base:0x%x\n    all_sectors:0x%x\n    "
           "inode_bitmap_lba:0x%x\n    inode_bitmap_sectors:0x%x\n    "
           "inode_table_lba:0x%x\n    inode_table_sectors:0x%x\n    "
           "data_start_lba:0x%x\n",
           sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt,
           sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba,
           sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects,
           sb.data_start_lba);

    struct disk* hd = part->my_disk;
    // 把超级块写入1扇区
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("    super_block_lba:0x%x\n", part->start_lba + 1);
    uint32_t buf_size = (sb.block_bitmap_sects >= sb.inode_bitmap_sects ?
                             sb.block_bitmap_sects :
                             sb.inode_bitmap_sects);
    buf_size =
        (buf_size >= sb.inode_table_sects ? buf_size : sb.inode_table_sects) *
        SECTOR_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);

    // 把块位图初始化并写入sb.block_bitmap_lba
    buf[0] |= 0x01;
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t block_bitmap_last_bit = block_bitmap_bit_len % 8;
    uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
    // last_size是指位图最后一个扇区中, 不足一个扇区的其余部分
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
    buf[0] |= 0x1;  // 第0个inode给根目录

    ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

    // 将inode数组初始化, 并写入sb.inode_table_lba
    memset(buf, 0, buf_size);
    struct inode* i = (struct inode*)buf;
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
    p_de->i_no = 0;  // 根目录的父目录是自己
    p_de->f_type = FT_DIRECTORY;
    ide_write(hd, sb.data_start_lba, buf, 1);

    printk("    root_dir_lba:0x%x\n", sb.data_start_lba);
    printk("%s format done\n", part->name);
    sys_free(buf);
}

// 例如: 输入/usr/share/xxx, 把usr parse出来, 存到name_store里面
static char* path_parse(char* pathname, char* name_store) {
    if (pathname[0] == '/') {       // 跳过根目录
        while (*pathname == '/') {  //  ///usr/  前面很多个/都要跳过
            pathname++;
        }
    }
    while (*pathname != '/' && *pathname != 0) {
        *name_store = *pathname;
        name_store++;
        pathname++;
    }
    if (pathname[0] == 0) { return NULL; }
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
        if (p) {  // p不为NULL, 就继续分析路径
            p = path_parse(p, name);
        }
    }
    return depth;
}

// 搜索到file, 就返回inode no, 否则返回-1
static int search_file(const char* pathname,
                       struct path_search_record* searched_record) {
    // 如果搜索的是根目录, 直接返回根目录的信息
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||
        !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type = FT_DIRECTORY;
        searched_record->searched_path[0] = 0;  // 搜索路径置空
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
            if (sub_path) { sub_path = path_parse(sub_path, name); }
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
        } else {  // 找不到返回-1
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
        printk(
            "can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth =
        path_depth_cnt(searched_record.searched_path);

    if (pathname_depth !=
        path_searched_depth) {  // 这两个数不一样, 说明中间有目录不存在
        printk("cannot access %s: Not a directory, subpath %s is't exist\n",
               pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    if (!found && !(flags & O_CREAT)) {  // 没找到, 并且不是要创建文件, 返回-1
        printk("in path %s, file %s is`t exist\n",
               searched_record.searched_path,
               (strrchr(searched_record.searched_path, '/') + 1));
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found &&
               (flags & O_CREAT)) {  // 要创建文件, 并且文件已存在, 返回-1
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT) {
        case O_CREAT:
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir,
                             (strrchr(pathname, '/') + 1), flags);
            dir_close(searched_record.parent_dir);
            break;
        default: fd = file_open(inode_no, flags);
    }
    return fd;
}

static uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if (fd > 2) {
        uint32_t global_fd = fd_local2global(fd);
        ret = file_close(&file_table[global_fd]);
        running_thread()->fd_table[fd] = -1;
    }
    return ret;
}

// 在磁盘上搜索文件系统, 没有文件系统的话就格式化分区, 创建文件系统
void filesys_init() {
    uint8_t channel_no = 0, dev_no, part_idx = 0;
    struct super_block* sb_buf =
        (struct super_block*)sys_malloc(SECTOR_SIZE);  // 用来存储超级块
    if (sb_buf == NULL) { PANIC("alloc memory failed!"); }
    printk("searching filesystem......\n");
    while (channel_no < channel_cnt) {
        dev_no = 0;
        while (dev_no < 2) {
            if (dev_no == 0) {  // 跨过 hd60M.img
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];
            struct partition* part = hd->prim_parts;
            while (part_idx < 12) {   // 4个主分区 + 8个逻辑分区
                if (part_idx == 4) {  // 开始处理逻辑分区
                    part = hd->logic_parts;
                }
                if (part->sec_cnt != 0) {  // 如果分区存在
                    memset(sb_buf, 0, SECTOR_SIZE);
                    // 将超级块读入
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    // 通过magic判断是什么文件系统
                    if (sb_buf->magic == 0x19590318) {
                        printk("%s has filesystem\n", part->name);
                    } else {
                        printk("formatting %s's partition %s......\n", hd->name,
                               part->name);
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
    while (fd_idx < MAX_FILE_OPEN) { file_table[fd_idx++].fd_inode = NULL; }
}

// 把buf中连续count个字节写入文件描述符fd, 成功返回写入的字节数, 失败返回-1
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if (fd < 0) {
        printk("sys_write: fd error\n");
        return -1;
    }
    if (fd == stdout_no) {
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }
    uint32_t global_fd = fd_local2global(fd);
    struct file* wr_file = &file_table[global_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag "
                        "O_RDWR or O_WRONLY\n");
        return -1;
    }
}

// 从fd读count个字节到buf中
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    if (fd < 0) {
        printk("sys_read: fd error\n");
        return -1;
    }
    ASSERT(buf != NULL);
    uint32_t global_fd = fd_local2global(fd);
    return file_read(&file_table[global_fd], buf, count);
}

// lseek, 根据whence对fd进行offset
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 0) {
        printk("sys_lseek: fd error\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t global_fd = fd_local2global(fd);
    struct file* pf = &file_table[global_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = (int32_t)pf->fd_pos + offset; break;
        case SEEK_END: new_pos = file_size + offset; break;
    }
    if (new_pos < 0 || new_pos > (file_size - 1)) { return -1; }
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

// 删除普通文件, 成功返回0, 失败返回-1
int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    // 搜索, 也就是先检查文件存不存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    if (inode_no == -1) {  // 文件不存在
        printk("file %s not found\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }
    if (searched_record.file_type ==
        FT_DIRECTORY) {  // 类型不对, 文件类型不能是目录
        printk(
            "can't delete a directory with unlink(), use rmdir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    // 检查文件是否已经打开, 打开的不能删
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL &&
            (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no) {
            break;
        }
        file_idx++;
    }
    if (file_idx < MAX_FILE_OPEN) {  // 文件已经打开
        dir_close(searched_record.parent_dir);
        printk("file %s is in use, not allow to delete!\n", pathname);
        return -1;
    }
    ASSERT(file_idx == MAX_FILE_OPEN);

    void* io_buf = sys_malloc(SECTOR_SIZE + SECTOR_SIZE);
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        printk("sys_unlink: malloc for io_buf failed\n");
        return -1;
    }

    struct dir* parent_dir = searched_record.parent_dir;
    delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
    inode_release(cur_part, inode_no);
    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

// 创建目录 pathname, 成功返回0, 失败返回-1
int32_t sys_mkdir(const char* pathname) {
    uint8_t rollback_step = 0;  // 用于操作失败时回滚各资源状态
    void* io_buf = sys_malloc(SECTOR_SIZE * 2);
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = -1;
    inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) {  // 如果找到了同名目录或文件,失败返回
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        rollback_step = 1;
        goto rollback;
    } else {  // 若未找到,也要判断是在最终目录没找到还是某个中间目录不存在
        uint32_t pathname_depth = path_depth_cnt((char*)pathname);
        uint32_t path_searched_depth =
            path_depth_cnt(searched_record.searched_path);
        // 先判断是否把pathname的各层目录都访问到了,即是否在某个中间目录就失败了
        if (pathname_depth !=
            path_searched_depth) {  // 说明并没有访问到全部的路径,某个中间目录是不存在的
            printk("sys_mkdir: can`t access %s, subpath %s is`t exist\n",
                   pathname, searched_record.searched_path);
            rollback_step = 1;
            goto rollback;
        }
    }

    struct dir* parent_dir = searched_record.parent_dir;
    // 目录名称后可能会有字符'/',所以最好直接用searched_record.searched_path,无'/'
    char* dirname = strrchr(searched_record.searched_path, '/') + 1;

    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        rollback_step = 1;
        goto rollback;
    }

    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);  // 初始化i结点

    uint32_t block_bitmap_idx = 0;  // 用来记录block对应于block_bitmap中的索引
    int32_t block_lba = -1;
    // 为目录分配一个块,用来写入目录.和..
    block_lba = block_bitmap_alloc(cur_part);
    if (block_lba == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 2;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_lba;
    // 每分配一个块就将位图同步到硬盘
    block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
    ASSERT(block_bitmap_idx != 0);
    bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

    // 将当前目录的目录项'.'和'..'写入目录
    memset(io_buf, 0, SECTOR_SIZE * 2);  // 清空io_buf
    struct dir_entry* p_de = (struct dir_entry*)io_buf;

    // 初始化当前目录"."
    memcpy(p_de->filename, ".", 1);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;

    p_de++;
    // 初始化当前目录".."
    memcpy(p_de->filename, "..", 2);
    p_de->i_no = parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;
    ide_write(cur_part->my_disk, new_dir_inode.i_sectors[0], io_buf, 1);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    // 在父目录中添加自己的目录项
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, SECTOR_SIZE * 2);  // 清空io_buf
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        // sync_dir_entry中将block_bitmap通过bitmap_sync同步到硬盘
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    // 父目录的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    // 将新创建目录的inode同步到硬盘
    memset(io_buf, 0, SECTOR_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    // 将inode位图同步到硬盘
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    sys_free(io_buf);

    // 关闭所创建目录的父目录
    dir_close(searched_record.parent_dir);
    return 0;

// 创建文件或目录需要创建相关的多个资源,若某步失败则会执行到下面的回滚步骤
rollback:  // 因为某步骤操作失败而回滚
    switch (rollback_step) {
        case 2:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            // 如果新文件的inode创建失败,之前位图中分配的inode_no也要恢复
        case 1:
            // 关闭所创建目录的父目录
            dir_close(searched_record.parent_dir);
            break;
    }
    sys_free(io_buf);
    return -1;
}

// 目录打开成功后返回目录指针, 失败返回NULL
struct dir* sys_opendir(const char* name) {
    ASSERT(strlen(name) < MAX_PATH_LEN);
    // 如果是根目录'/',直接返回&root_dir
    if (name[0] == '/' && (name[1] == 0 || name[0] == '.')) {
        return &root_dir;
    }

    // 先检查待打开的目录是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(name, &searched_record);
    struct dir* ret = NULL;
    if (inode_no == -1) {
        // 如果找不到目录,提示不存在的路径
        printk("In %s, sub path %s not exist\n", name,
               searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", name);
        } else if (searched_record.file_type == FT_DIRECTORY) {
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

// 成功关闭目录dir返回0, 失败返回-1
int32_t sys_closedir(struct dir* dir) {
    int32_t ret = -1;
    if (dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

// 读取目录dir的1个目录项,成功后返回其目录项地址,到目录尾时或出错时返回NULL
struct dir_entry* sys_readdir(struct dir* dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

// 把目录dir的指针dir_pos置0
void sys_rewinddir(struct dir* dir) {
    dir->dir_pos = 0;
}

// rmdir, 只能删除空目录, 成功返回0, 失败返回-1
int32_t sys_rmdir(const char* pathname) {
    // 先检查待删除的文件是否存在
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);
    int retval = -1;  // 默认返回值
    if (inode_no == -1) {
        printk("In %s, sub path %s not exist\n", pathname,
               searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        } else {
            struct dir* dir = dir_open(cur_part, inode_no);
            if (!dir_is_empty(dir)) {  // 非空目录不可删除
                printk("dir %s is not empty, it is not allowed to delete a "
                       "nonempty directory!\n",
                       pathname);
            } else {
                if (!dir_remove(searched_record.parent_dir, dir)) {
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}

// 获得父目录的inode编号
static uint32_t get_parent_dir_inode_nr(uint32_t child_inode_nr, void* io_buf) {
    struct inode* child_dir_inode = inode_open(cur_part, child_inode_nr);
    // 目录中的目录项".."中包括父目录inode编号, ".."位于目录的第0块
    uint32_t block_lba = child_dir_inode->i_sectors[0];
    ASSERT(block_lba >= cur_part->sb->data_start_lba);
    inode_close(child_dir_inode);
    ide_read(cur_part->my_disk, block_lba, io_buf, 1);
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    // 第0个目录项是".", 第1个目录项是".."
    ASSERT(dir_e[1].i_no < 4096 && dir_e[1].f_type == FT_DIRECTORY);
    return dir_e[1].i_no;  // 返回..即父目录的inode编号
}

/* 在inode编号为p_inode_nr的目录中查找inode编号为c_inode_nr的子目录的名字,
 * 将名字存入缓冲区path.成功返回0,失败返-1 */
static int get_child_dir_name(uint32_t p_inode_nr,
                              uint32_t c_inode_nr,
                              char* path,
                              void* io_buf) {
    struct inode* parent_dir_inode = inode_open(cur_part, p_inode_nr);
    // 填充all_blocks,将该目录的所占扇区地址全部写入all_blocks
    uint8_t block_idx = 0;
    uint32_t all_blocks[140] = {0}, block_cnt = 12;
    while (block_idx < 12) {
        all_blocks[block_idx] = parent_dir_inode->i_sectors[block_idx];
        block_idx++;
    }
    if (parent_dir_inode->i_sectors[12]) {
        // 若包含了一级间接块表,将共读入all_blocks.
        ide_read(cur_part->my_disk, parent_dir_inode->i_sectors[12],
                 all_blocks + 12, 1);
        block_cnt = 140;
    }
    inode_close(parent_dir_inode);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = (512 / dir_entry_size);
    block_idx = 0;
    // 遍历所有块
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx]) {
            // 如果相应块不为空则读入相应块
            ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
            uint8_t dir_e_idx = 0;
            // 遍历每个目录项
            while (dir_e_idx < dir_entrys_per_sec) {
                if ((dir_e + dir_e_idx)->i_no == c_inode_nr) {
                    strcat(path, "/");
                    strcat(path, (dir_e + dir_e_idx)->filename);
                    return 0;
                }
                dir_e_idx++;
            }
        }
        block_idx++;
    }
    return -1;
}

// 把当前工作目录绝对路径写入buf, size是buf的大小. 失败返回NULL
char* sys_getcwd(char* buf, uint32_t size) {
    if (buf == NULL) return NULL;
    void* io_buf = sys_malloc(SECTOR_SIZE);
    if (io_buf == NULL) return NULL;

    struct task_struct* cur_thread = running_thread();
    int32_t parent_inode_nr = 0;
    int32_t child_inode_nr = cur_thread->cwd_inode_nr;

    // 最大支持4096个inode
    ASSERT(child_inode_nr >= 0 && child_inode_nr < 4096);
    // 若当前目录是根目录,直接返回'/'
    if (child_inode_nr == 0) {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};  // 用来做全路径缓冲区

    /* 从下往上逐层找父目录,直到找到根目录为止.
     * 当child_inode_nr为根目录的inode编号(0)时停止,
     * 即已经查看完根目录中的目录项 */
    while (child_inode_nr) {
        parent_inode_nr = get_parent_dir_inode_nr(child_inode_nr, io_buf);
        if (get_child_dir_name(parent_inode_nr, child_inode_nr,
                               full_path_reverse, io_buf) == -1) {
            // 或未找到名字,失败退出
            sys_free(io_buf);
            return NULL;
        }
        child_inode_nr = parent_inode_nr;
    }
    ASSERT(strlen(full_path_reverse) <= size);
    /* 至此full_path_reverse中的路径是反着的,
     * 即子目录在前(左),父目录在后(右) ,
     * 现将full_path_reverse中的路径反置 */
    char* last_slash;  // 用于记录字符串中最后一个斜杠地址
    while (last_slash = strrchr(full_path_reverse, '/')) {
        uint16_t len = strlen(buf);
        strcpy(buf + len, last_slash);
        // 在full_path_reverse中添加结束字符,做为下一次执行strcpy中last_slash的边界
        *last_slash = 0;
    }
    sys_free(io_buf);
    return buf;
}

// 更改当前工作目录为绝对路径path, 成功则返回0, 失败返回-1
int32_t sys_chdir(const char* path) {
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1 && searched_record.file_type == FT_DIRECTORY) {
        // 找到文件并且文件类型是目录
        running_thread()->cwd_inode_nr = inode_no;
        dir_close(searched_record.parent_dir);
        return 0;
    }
    // 找不到文件或者文件类型不是目录
    dir_close(searched_record.parent_dir);
    return -1;
}

// 根据path代表的文件, 在buf中填写相关信息, 成功返回0, 失败返回-1
int32_t sys_stat(const char* path, struct stat* buf) {
    // 若直接查看根目录'/'
    if (!strcmp(path, "/") || !strcmp(path, "/.") || !strcmp(path, "/..")) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }

    int32_t ret = -1;  // 默认返回值
    struct path_search_record searched_record;
    // 记得初始化或清0, 否则栈中信息不知道是什么
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int inode_no = search_file(path, &searched_record);
    if (inode_no != -1) {
        struct inode* obj_inode =
            inode_open(cur_part, inode_no);  // 只为获得文件大小
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    } else {
        printk("sys_stat: %s not found\n", path);
    }
    dir_close(searched_record.parent_dir);
    return ret;
}
