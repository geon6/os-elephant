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
; 根据图4-8构造选择子
SELECTOR_CODE equ (0x0001<<3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002<<3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

; dw dd共6byte, 48位. lgdt指令需要48位内存数据
gdt_ptr         dw GDT_LIMIT
                dd GDT_BASE
loadermsg       db '2 loader in real.'

loader_start:
; 打印字符串
    mov sp, LOADER_BASE_ADDR
    mov bp, loadermsg
    mov cx, 17
    mov ax, 0x1301
    mov bx, 0x001f
    mov dx, 0x1800
    int 0x10

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
