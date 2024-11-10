#include "memory.h"
#include "bitmap.h"
#include "debug.h"
#include "global.h"
#include "interrupt.h"
#include "list.h"
#include "print.h"
#include "stdint.h"
#include "string.h"
#include "sync.h"

#define PG_SIZE 4096

// 0xc009f000是内核主线程栈顶, 0xc009e000是内核主线程的pcb
// 0x.....fff往下放的是进程在0特权级下所用的栈
// 一个page的bitmap可以表示128MB内存, bitmap放在0xc009a000
// 可以放四个page的bitmap, 也就是本系统最多支持512MB内存
#define MEM_BITMAP_BASE 0xc009a000

// 0xc0000000是内核从虚拟地址3G, 100000表示跳过1MB
// K_HEAP_START是内核使用的堆空间的起始虚拟地址
// 跳过1MB是因为内核的低1MB映射到物理地址的低1MB了
// 为了让虚拟地址连续, 就只能跳过1MB(当然也可以不让虚拟地址连续, 定义为0xd0000000)
#define K_HEAP_START 0xc0100000

// page directory entry index, page table entry index
// pde idx就是虚拟地址的高10位
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
// pte idx就是虚拟地址的中间10位
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

// 将内存分为两个内存池, 一个是kernel, 一个是user
// 内存池结构
struct pool {
    struct bitmap pool_bitmap;  // 内存池需要一个bitmap管理
    uint32_t phy_addr_start;    // 内存池所管理的物理地址起始地址
    uint32_t pool_size;         // 容量, 字节为单位
    struct lock lock;
};

struct arena {
    struct mem_block_desc* desc;  // 每个arena关联一个mem_block_desc

    // large为true时, cnt表示page frame个数
    // large为false时, cnt表示空间剩余的mem_block数量
    uint32_t cnt;
    bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT];  // 内核的内存块描述符数组
struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr;  // 用来给内核分配虚拟地址

// pf表示的虚拟内存池中申请pg_cnt个虚拟页, 成功返回虚拟页的起始地址, 失败返回NULL
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if (pf == PF_KERNEL) {  // 内核内存池
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) { return NULL; }
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    } else {                                         // 用户内存池
        struct task_struct* cur = running_thread();  // 获取用户进程
        // 从用户进程的虚拟地址空间分配虚拟页
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) { return NULL; }
        while (cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++,
                       1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void*)vaddr_start;
}

// 得到虚拟地址vaddr对应的pte指针
uint32_t* pte_ptr(uint32_t vaddr) {
    // PTE_IDX * 4是因为一个PTE占四字节
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) +
                                PTE_IDX(vaddr) * 4);
    return pte;
}

// 得到虚拟地址vaddr对应的pde指针
uint32_t* pde_ptr(uint32_t vaddr) {
    // PDE_IDX * 4是因为一个PDE占四字节
    uint32_t* pde = (uint32_t*)((0xfffff000 + PDE_IDX(vaddr) * 4));
    return pde;
}

// 从m_pool中分配一个物理页
static void* palloc(struct pool* m_pool) {
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
    if (bit_idx == -1) { return NULL; }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
    uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
    return (void*)page_phyaddr;
}

// 在页表中添加虚拟地址到物理地址的映射
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
    uint32_t vaddr = (uint32_t)_vaddr, page_phyaddr = (uint32_t)_page_phyaddr;
    uint32_t* pde = pde_ptr(vaddr);  // pde的虚拟地址
    uint32_t* pte = pte_ptr(vaddr);  // pte的虚拟地址

    if (*pde & 0x00000001) {  // 判断p位, 为1表示该表已经存在
        ASSERT(!(*pte & 0x00000001));  // 要求page table entry之前不存在
        if (!(*pte & 0x00000001)) {
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        } else {  // 现在不会执行到这里, 会被ASSERT拦截下来
            PANIC("pte repeat");
            *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
        }
    } else {  // pde不存在, 先创建pde
        // 因为是页表需要的物理内存, 所以从kernel pool申请.
        uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
        *pde = (pde_phyaddr | PG_US_U | PG_RW_W |
                PG_P_1);  // pte所在页表的物理地址
        // 为什么清除pte就是清除新分配到的物理地址?? 因为这个pte代表的物理地址需要从pde算出来
        // 而pde已经被更新了
        memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
        ASSERT(!(*pte & 0x00000001));
        *pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }
}

// 分配pg_cnt个page
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    // 从pf代表的虚拟内存池中分配pg_cng个虚拟页
    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL) { return NULL; }

    // 分配虚拟页成功, 下面分配物理页
    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    while (cnt-- > 0) {
        void* page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL) {
            // TODO: 分配失败需要把已分配的物理页回收
            return NULL;
        }
        page_table_add((void*)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}

// 从内核物理内存池中申请pg_cng个页, 失败返回NULL, 成功返回虚拟地址.
// 这个函数分配成功指的是分配虚拟页和物理页都成功了, 并且在页表中建立了映射.
void* get_kernel_pages(uint32_t pg_cnt) {
    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
    if (vaddr != NULL) { memset(vaddr, 0, pg_cnt * PG_SIZE); }
    return vaddr;
}

// 在用户空间申请pg_cng个页, 返回虚拟地址
void* get_user_pages(uint32_t pg_cnt) {
    lock_acquire(&user_pool.lock);
    void* vaddr = malloc_page(PF_USER, pg_cnt);
    memset(vaddr, 0, pg_cnt * PG_SIZE);
    lock_release(&user_pool.lock);
    return vaddr;
}

// 从pf代表的内存池中搞一个物理页, 并与虚拟地址vaddr相关联. 一次只分配一页
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    // 将pf对应的bitmap对应vaddr位置的bit置1
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    // pgdir不空说明thread有自己的页目录表和页表. 是用户进程
    if (cur->pgdir != NULL && pf == PF_USER) {  // 用户进程申请用户内存
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
    } else if (cur->pgdir == NULL && pf == PF_KERNEL) {  // 内核线程申请内核内存
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx > 0);
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("get_a_page:not allow kernel alloc userspace or user alloc "
              "kernelspace by get_a_page");
    }

    // 获取物理页, 并得到物理地址
    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) { return NULL; }

    // 添加虚拟地址到物理地址的映射
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

// 返回vaddr对应的物理地址
uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

// 对desc_array里的mem_block_desc进行初始化
void block_desc_init(struct mem_block_desc* desc_array) {
    uint16_t desc_idx, block_size = 16;  // block size默认为16B
    for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
        desc_array[desc_idx].block_size = block_size;
        desc_array[desc_idx].blocks_per_arena =
            (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[desc_idx].free_list);
        block_size *= 2;  // 更新为下一规格的内存块
    }
}

// 返回arena中第idx个内存块的地址
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
    // 每个struct arena是该arena的元信息, 存放在每个页的最前面
    // 首先加上sizeof(struct arena), 跳过元信息
    // 然后根据block_size和idx, 返回对应的地址
    return (struct mem_block*)((uint32_t)a + sizeof(struct arena) +
                               idx * a->desc->block_size);
}

// 根据block, 返回对应arena的地址
static struct arena* block2arena(struct mem_block* b) {
    // b的地址, 取前面的20位, 就是arena的地址
    return (struct arena*)((uint32_t)b & 0xfffff000);
}

void* sys_malloc(uint32_t size) {
    enum pool_flags PF;
    struct pool* mem_pool;
    uint32_t pool_size;
    struct mem_block_desc* descs;
    struct task_struct* cur_thread = running_thread();

    if (cur_thread->pgdir ==
        NULL) {  // 没有自己的页目录表和页表, 说明是内核线程
        PF = PF_KERNEL;
        pool_size = kernel_pool.pool_size;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    } else {  // 用户线程
        PF = PF_USER;
        pool_size = user_pool.pool_size;
        mem_pool = &user_pool;
        descs = cur_thread->u_block_desc;
    }

    if (!(size > 0 && size < pool_size)) {  // 错误的size, 不分配
        return NULL;
    }

    struct arena* a;
    struct mem_block* b;
    lock_acquire(&mem_pool->lock);

    if (size > 1024) {  // 超过1024B的, 分配page
        uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);
        a = malloc_page(PF, page_cnt);

        if (a != NULL) {  // 申请page成功
            // 分配的内存清0
            memset(a, 0, page_cnt * PG_SIZE);
            a->desc = NULL;
            a->cnt = page_cnt;
            a->large = true;
            lock_release(&mem_pool->lock);
            return (void*)(a + 1);  // 跳过arena的元信息, 返回后面开始的地址
        } else {                    // 申请page失败
            lock_release(&mem_pool->lock);
            return NULL;
        }
    } else {  // size 小于等于 1024B
        uint8_t desc_idx;
        for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx++) {
            if (size <= descs[desc_idx].block_size) { break; }
        }
        if (list_empty(&descs[desc_idx].free_list)) {
            // 没有mem_block了, 先分配一个page
            a = malloc_page(PF, 1);
            if (a == NULL) {
                lock_release(&mem_pool->lock);
                return NULL;
            }
            // 清0
            memset(a, 0, PG_SIZE);
            // 填写元信息
            a->desc = &descs[desc_idx];
            a->large = false;
            a->cnt = descs[desc_idx].blocks_per_arena;

            // 将page剩余的空间分配为block, 由于刚刚清0了, 现在里面啥也没有
            // arena2block将idx转为block的地址, 把block添加到list里,
            // 添加的过程会修改b->free_elem的prev和next, 也就是修改
            // page里面的内容. 以后把block分配出去的时候, 再把内容清空就行了
            uint32_t block_idx;
            enum intr_status old_status = intr_disable();
            for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena;
                 block_idx++) {
                b = arena2block(a, block_idx);
                ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
                list_append(&a->desc->free_list, &b->free_elem);
            }
            intr_set_status(old_status);
        }

        // 分配内存块
        b = elem2entry(struct mem_block, free_elem,
                       list_pop(&(descs[desc_idx].free_list)));
        memset(b, 0, descs[desc_idx].block_size);
        a = block2arena(b);
        a->cnt--;
        lock_release(&mem_pool->lock);
        return (void*)b;
    }
}

// 将pg_phy_addr所在page回收到物理内存池, 具体就是修改bitmap
void pfree(uint32_t pg_phy_addr) {
    struct pool* mem_pool;
    uint32_t bit_idx = 0;
    // 物理地址低的是内核用的, 通过比较大小就可以判断回收到内核的物理内存池还是用户的物理内存池
    if (pg_phy_addr >= user_pool.phy_addr_start) {  // 用户物理内存池
        mem_pool = &user_pool;
        bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
    } else {  // 内核物理内存池
        mem_pool = &kernel_pool;
        bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
    }
    bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

// 去掉vaddr对应的pte
static void page_table_pte_remove(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    *pte &= ~PG_P_1;
    asm volatile("invlpg %0" : : "m"(vaddr) : "memory");  // 更新tlb
}

// 在pf中, 释放以_vaddr开始的pg_cnt个虚拟页
static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;
    if (pf == PF_KERNEL) {  // 内核虚拟内存池
        bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
        }
    } else {  // 用户虚拟内存池
        struct task_struct* cur_thread = running_thread();
        bit_idx_start =
            (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
        while (cnt < pg_cnt) {
            bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap,
                       bit_idx_start + cnt++, 0);
        }
    }
}

// 在pf中, 释放以_vaddr开始的pg_cnt个物理页
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t pg_phy_addr;
    uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
    pg_phy_addr = addr_v2p(vaddr);  // 获取虚拟地址vaddr对应的物理地址

    ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

    if (pg_phy_addr >= user_pool.phy_addr_start) {  // 用户内存池
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);  // 对应的物理地址
            ASSERT((pg_phy_addr % PG_SIZE) == 0 &&
                   pg_phy_addr >= user_pool.phy_addr_start);
            pfree(pg_phy_addr);            // 释放物理页
            page_table_pte_remove(vaddr);  // 清除pte
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);  // 释放虚拟地址
    } else {                               // 内核内存池
        vaddr -= PG_SIZE;
        while (page_cnt < pg_cnt) {
            vaddr += PG_SIZE;
            pg_phy_addr = addr_v2p(vaddr);  // 对应的物理地址
            ASSERT((pg_phy_addr % PG_SIZE) == 0 &&
                   pg_phy_addr >= kernel_pool.phy_addr_start &&
                   pg_phy_addr < user_pool.phy_addr_start);
            pfree(pg_phy_addr);            // 释放物理页
            page_table_pte_remove(vaddr);  // 清除pte
            page_cnt++;
        }
        vaddr_remove(pf, _vaddr, pg_cnt);  // 释放虚拟地址
    }
}

// 回收ptr指向的内存
void sys_free(void* ptr) {
    ASSERT(ptr != NULL);
    if (ptr != NULL) {
        enum pool_flags PF;
        struct pool* mem_pool;

        if (running_thread()->pgdir == NULL) {  // 内核线程
            ASSERT((uint32_t)ptr >= K_HEAP_START);
            PF = PF_KERNEL;
            mem_pool = &kernel_pool;
        } else {
            PF = PF_USER;
            mem_pool = &user_pool;
        }

        lock_acquire(&mem_pool->lock);
        struct mem_block* b = ptr;  // ptr指向的位置转为mem_block
        struct arena* a = block2arena(b);  // mem_block转为arena, 用于获取元信息

        ASSERT(a->large == 0 || a->large == 1);
        if (a->desc == NULL && a->large == true) {  // large: 大于1024B的内存
            // 直接释放一整个page即可
            mfree_page(PF, a, a->cnt);
        } else {  // 小于等于1024B的内存块
            list_append(&a->desc->free_list, &b->free_elem);
            a->cnt += 1;  // free的block增加
            // 如果增加后free的page等于最大数, 说明整个page都free了, 释放整个page
            if (a->cnt == a->desc->blocks_per_arena) {
                // 释放page之前, 需要把block从free list中删除
                uint32_t block_idx;
                for (block_idx = 0; block_idx < a->desc->blocks_per_arena;
                     block_idx++) {
                    struct mem_block* b = arena2block(a, block_idx);
                    ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
                    list_remove(&b->free_elem);
                }
                mfree_page(PF, a, 1);
            }
        }
        lock_release(&mem_pool->lock);
    }
}

static void mem_pool_init(uint32_t all_mem) {
    put_str("   mem_pool_init start\n");

    // 页表大小, 为什么是这么大? 1个页目录表, 0和768指向同一个页表, 769-1022有254个页表, 共256. (中间的1-768呢?)
    uint32_t page_table_size = PG_SIZE * 256;

    uint32_t used_mem = page_table_size + 0x100000;
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE;

    uint16_t kernel_free_pages = all_free_pages / 2;
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;

    // bitmap中一位代表一个page, bitmap长度以字节为单位
    uint32_t kbm_length = kernel_free_pages / 8;
    uint32_t ubm_length = user_free_pages / 8;

    uint32_t kp_start = used_mem;
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

    kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
    user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

    put_str("   kernel_pool_bitmap_start:");
    put_int((int)kernel_pool.pool_bitmap.bits);
    put_str("   kernel_pool_phy_addr_start:");
    put_int(kernel_pool.phy_addr_start);
    put_str("\n");
    put_str("   user_pool_bitmap_start:");
    put_int((int)user_pool.pool_bitmap.bits);
    put_str("   user_pool_phy_addr_start:");
    put_int(user_pool.phy_addr_start);
    put_str("\n");

    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    kernel_vaddr.vaddr_bitmap.bits =
        (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);
    kernel_vaddr.vaddr_start = K_HEAP_START;
    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    put_str("   mem_pool_init done\n");
}

void mem_init() {
    put_str("mem_init start\n");
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb00));
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);  // 初始化内核的block desc
    put_str("mem_init done\n");
}