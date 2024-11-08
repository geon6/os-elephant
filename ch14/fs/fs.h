#ifndef __FS_FS_H
#define __FS_FS_H

#include "stdint.h"
#include "ide.h"

#define MAX_FILES_PER_PART 4096 // 每个分区最大文件数
#define BITS_PER_SECTOR 4096    // 每个扇区的位数
#define SECTOR_SIZE 512         // 扇区大小, 字节为单位
#define BLOCK_SIZE SECTOR_SIZE  // 块大小, 字节为单位
#define MAX_PATH_LEN 512        // 路径最大长度

enum file_types {
    FT_UNKNOWN,     // 不支持的文件类型
    FT_REGULAR,     // 普通文件
    FT_DIRECTORY    // 目录文件
};

enum oflags {
    O_RDONLY,
    O_WRONLY,
    O_RDWR,
    O_CREAT = 4
};

// 用来记录查找文件过程中, "走过的地方"
struct path_search_record {
    char searched_path[MAX_PATH_LEN]; // 父路径
    struct dir* parent_dir; // 父目录
    enum file_types file_type;
};

extern struct partition* cur_part;

int32_t path_depth_cnt(char* pathname);
int32_t sys_open(const char* pathname, uint8_t flags);
void filesys_init();

#endif
