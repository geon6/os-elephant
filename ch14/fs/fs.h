#ifndef __FS_FS_H
#define __FS_FS_H

#include "ide.h"
#include "stdint.h"

#define MAX_FILES_PER_PART 4096  // 每个分区最大文件数
#define BITS_PER_SECTOR 4096     // 每个扇区的位数
#define SECTOR_SIZE 512          // 扇区大小, 字节为单位
#define BLOCK_SIZE SECTOR_SIZE   // 块大小, 字节为单位
#define MAX_PATH_LEN 512         // 路径最大长度

enum file_types {
    FT_UNKNOWN,   // 不支持的文件类型
    FT_REGULAR,   // 普通文件
    FT_DIRECTORY  // 目录文件
};

enum oflags { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT = 4 };

// lseek
enum whence { SEEK_SET = 1, SEEK_CUR, SEEK_END };

// 用来记录查找文件过程中, "走过的地方"
struct path_search_record {
    char searched_path[MAX_PATH_LEN];  // 父路径
    struct dir* parent_dir;            // 父目录
    enum file_types file_type;
};

// 文件属性
struct stat {
    uint32_t st_ino;              // inode编号
    uint32_t st_size;             // 大小
    enum file_types st_filetype;  // 文件类型
};

extern struct partition* cur_part;

int32_t path_depth_cnt(char* pathname);

// 针对当前线程的局部fd的open close
int32_t sys_open(const char* pathname, uint8_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void* buf, uint32_t count);
int32_t sys_read(int32_t fd, void* buf, uint32_t count);
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_unlink(const char* pathname);
int32_t sys_mkdir(const char* pathname);
struct dir* sys_opendir(const char* name);
int32_t sys_closedir(struct dir* dir);
struct dir_entry* sys_readdir(struct dir* dir);
void sys_rewinddir(struct dir* dir);
int32_t sys_rmdir(const char* pathname);

void filesys_init();

#endif
