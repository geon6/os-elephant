; loader and kernel
LOADER_BASE_ADDR equ 0x900
LOADER_START_SECTOR equ 0x2

; gdt 配合图4-5以及表4-10理解
DESC_G_4K equ 1_00000000000000000000000b            ; 段界限的单位为4KB
DESC_D_32 equ 1_0000000000000000000000b             ; 0表示操作数16位, 1表示32位
DESC_L equ 0_000000000000000000000b                 ; 0表示32位系统, 1表示64位系统
DESC_AVL equ 0_00000000000000000000b                ; AVL(available)位没用, 随意
DESC_LIMIT_CODE2 equ 1111_0000000000000000b         ; 
DESC_LIMIT_DATA2 equ DESC_LIMIT_CODE2               ;
DESC_LIMIT_VIDEO2 equ 0000_000000000000000b         ;
DESC_P equ 1_000000000000000b                       ; P(present)表示是否在内存中
DESC_DPL_0 equ 00_0000000000000b                    ; 0,1,2,3是特权级别, 0最高,  
DESC_DPL_1 equ 01_0000000000000b                    ; 进入保护模式后, 默认为0
DESC_DPL_2 equ 10_0000000000000b                    ; 保护模式下的代码已经是os的一部分
DESC_DPL_3 equ 11_0000000000000b                    ; 用户程序一般是3
                                                    ; 某些指令只能在0下执行, 保证安全
DESC_S_CODE equ 1_000000000000b                     ; S为1表示非系统段
DESC_S_DATA equ DESC_S_CODE                         ; CODE和DATA都是非系统段
DESC_S_sys equ 0_000000000000b                      ; 系统段
                                                    ; type字段分为XRCA
                                                    ; X是可执行, R是可读, W是可写
                                                    ; C是一致性Conforming
                                                    ; A是Accessed由CPU设置
                                                    ; E是拓展方向, 有向上和向下
                                                    ; 栈用向下扩展(1), 代码段和数据段用向上扩展(0)
DESC_TYPE_CODE equ 1000_00000000b                   ; XCRA=1000, 代码段可执行
DESC_TYPE_DATA equ 0010_00000000b                   ; XEWA=0010, 数据段可写

; 代码段, 数据段, 显存段的高4字节
DESC_CODE_HIGH4  equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_CODE2  + DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00
DESC_DATA_HIGH4  equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_DATA2  + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00
DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0B

; 选择子用来索引GDT
; 选择子描述符
; 第0, 1两位是RPL. RPL是特权等级(Request Page Level)
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
; 第2位是TI(Table Indicator)
; TI为0表示选择子在GDT中, 为1表示在LDT中.
TI_GDT equ 000b
TI_LDT equ 100b