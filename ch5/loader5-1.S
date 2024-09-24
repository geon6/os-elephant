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

    jmp $
