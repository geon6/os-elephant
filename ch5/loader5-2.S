; loader主要是实模式到保护模式的过渡
%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
jmp loader_start

; gdt 根据图4-5理解
GDT_BASE:           dd 0x00000000 ; 低4字节, 低2字节是段界限的0-15位, 高2字节是段基址的0-15位
                    dd 0x00000000 ; 高4字节, 有很多字段, 根据图4-5理解
CODE_DESC:          dd 0x0000FFFF 
                    dd DESC_CODE_HIGH4 ; 高4字节的计算比较复杂, 具体看boot.inc
DATA_STACK_DESC:    dd 0x0000FFFF
                    dd DESC_DATA_HIGH4
                                  ; 表1-1, 显存地址0xb8000-0xbffff, 段单位4K
VIDEO_DESC:         dd 0x80000007 ; 段界限limit=(0xbffff-0xb80000)/4k=0x7
                    dd DESC_VIDEO_HIGH4

GDT_SIZE equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1

times 60 dq 0 ; dq是define quad-word, 4个word, 即8个byte. 预留60个描述符的空位

; 内存容量, 字节为单位
; loader加载地址为0x900, 这里用了0x200, 所以total_mem_bytes在内存中的地址为0xb00
total_mem_bytes dd 0

; dw dd共6byte, 48位. lgdt指令需要48位内存数据
gdt_ptr         dw GDT_LIMIT
                dd GDT_BASE

; total_mem_bytes4 + gdt_ptr6 + ards_buf244 + ards_nr2 = 256bytes
ards_buf times 244 db 0
ards_nr dw 0            ; 用于记录ARDS结构体数量

loader_start:
; int 15h eax = 0000E820h, edx = 534D4150h
    xor ebx, ebx        ; ebx置0
    mov edx, 0x534d4150
    mov di, ards_buf
.e820_mem_get_loop:
    mov eax, 0x0000e820
    mov ecx, 20
    int 0x15
    jc .e820_mem_get_loop
    add di, cx
    inc word [ards_nr]
    cmp ebx, 0
    jnz .e820_mem_get_loop
    mov cx, [ards_nr]
    mov ebx, ards_buf
    xor edx, edx
.find_max_mem_area:
    mov eax, [ebx]
    add eax, [ebx + 8]
    add ebx, 20
    cmp edx, eax
    jge .next_ards
    mov edx, eax
.next_ards:
    loop .find_max_mem_area
    jmp .mem_get_ok

.e820_failed_so_try_e801:
    mov ax, 0xe801
    int 0x15
    jc .e801_failed_so_try88
    mov cx, 0x400
    mul cx
    shl edx, 16
    and eax, 0x0000FFFF
    or edx, eax
    add edx, 0x100000
    mov esi, edx
    xor eax, eax
    mov ax, bx
    mov ecx, 0x10000
    mul ecx
    add esi, eax
    mov edx, esi
    jmp .mem_get_ok

.e801_failed_so_try88:
    mov ah, 0x88
    int 0x15
    jc .error_hlt
    and eax, 0x0000ffff
    mov cx, 0x400
    mul cx
    shl edx, 16
    or edx, eax
    add edx, 0x100000

.mem_get_ok:
    mov [total_mem_bytes], edx
    

; 进入保护模式
    ; 1. 打开A20
    in al, 0x92
    or al, 0000_0010B
    out 0x92, al

    ; 2. 加载GDT
    lgdt [gdt_ptr]

    ; 3. cr0第0位置1
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start

.error_hlt:
    hlt

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    mov byte [gs:160], 'P'

    call setup_page
    sgdt [gdt_ptr]
    mov ebx, [gdt_ptr + 2]
    or dword [ebx + 0x18 + 4], 0xc000_0000
    add dword [gdt_ptr + 2], 0xc000_0000

    add esp, 0xc000_0000
    mov eax, PAGE_DIR_TABLE_POS
    mov cr3, eax

    mov eax, cr0
    or eax, 0x8000_0000
    mov cr0, eax

    lgdt [gdt_ptr]
    mov byte [gs:160], 'V'
    jmp $

; setup_page函数用于创建页目录和页表
setup_page:
    ; 下面几行首先初始化一些变量, 然后清楚页目录占用的空间, 全部置0
    mov ecx, 4096 ; loop使用ecx寄存器, 代表循环4096次
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_POS + esi], 0
    inc esi
    loop .clear_page_dir

    ; 创建页目录项(PDE)
.create_pde:
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x1000 ; 
    mov ebx, eax
    or eax, PG_US_U | PG_RW_W | PG_P
    ; 两个目录项都存为第一个页表的地址, 每个页表表示4MB内存, 这是为了将地址映射为内核地址做准备
    ; 放在第0个页目录项是为了运行loader, 因为loader在物理地址的1MB内.
    ; 放在第768个页目录项是为了运行kernel, 因为kernel的内存在3MB后面.
    mov [PAGE_DIR_TABLE_POS + 0x0], eax
    mov [PAGE_DIR_TABLE_POS + 0xc00], eax
    ; 0xc00表示第768个页表占用的目录项, 0xc00以上的目录项用于内核空间
    ; 也就是说: 0xc000_0000 - 0xffff_ffff这1G空间属于内核
    ; 0x0000_0000 - 0xbfff_ffff 这3G空间属于用户进程
    sub eax, 0x1000
    mov [PAGE_DIR_TABLE_POS + 4092], eax ; 最后一个目录项指向目录表的地址, 4092是因为一个目录项4B

; 创建页表项
    mov ecx, 256    ; 1MB内存 / 每个页4KB = 256
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
    mov [ebx + esi * 4], edx
    add edx, 4096
    inc esi 
    loop .create_pte

; 创建内核其他页表的PDE
    mov eax, PAGE_DIR_TABLE_POS
    add eax, 0x2000
    or eax, PG_US_U | PG_RW_W | PG_P
    mov ebx, PAGE_DIR_TABLE_POS
    mov ecx, 254
    mov esi, 769    ; 769 - 1022 
.create_kernel_pde:
    mov [ebx + esi * 4], eax
    inc esi
    add eax, 0x1000
    loop .create_kernel_pde
    ret