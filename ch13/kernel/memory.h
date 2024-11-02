#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"
#include "list.h"

// 内存池标记, 用于判断用哪个内存池
enum pool_flags {
    PF_KERNEL = 1,
    PF_USER = 2
};

#define PG_P_1  1       // page的present位, 代表是否存在于内存
#define PG_P_0  0
#define PG_RW_R 0       // 读写权限, 0表示读/执行
#define PG_RW_W 2       // 2表示读/写/执行
#define PG_US_S 0       // user or system, 特权级
#define PG_US_U 4

// 虚拟地址池, 用于虚拟地址管理
struct virtual_addr {
    struct bitmap vaddr_bitmap;
    uint32_t vaddr_start;
};

struct mem_block {
    struct list_elem free_elem;
};

// 内存块描述符
struct mem_block_desc {
    uint32_t block_size;
    uint32_t blocks_per_arena;
    struct list free_list;
};

#define DESC_CNT 7  // 内存块描述符的个数
// 内存块大小: 16, 32, 64, 128, 256, 512, 1024

extern struct pool kernel_pool, user_pool;

// 得到虚拟地址vaddr对应的pte指针
uint32_t* pte_ptr(uint32_t vaddr);

// 得到虚拟地址vaddr对应的pde指针
uint32_t* pde_ptr(uint32_t vaddr);

// 分配pg_cnt个page
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);

// 从内核物理内存池中申请pg_cng个页, 失败返回NULL, 成功返回虚拟地址.
// 这个函数分配成功指的是分配虚拟页和物理页都成功了, 并且在页表中建立了映射.
void* get_kernel_pages(uint32_t pg_cnt);

void* get_a_page(enum pool_flags pf, uint32_t vaddr);

uint32_t addr_v2p(uint32_t vaddr);

void block_desc_init(struct mem_block_desc* desc_array);

void* sys_malloc(uint32_t size);

void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);

void pfree(uint32_t pg_phy_addr);

void sys_free(void* ptr);

void mem_init(void);

#endif