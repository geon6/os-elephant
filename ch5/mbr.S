%include "boot.inc"
; include是预处理, 这里的boot.inc是配置文件
SECTION MBR vstart=0x7c00
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov gs, ax

    ; 清屏
    mov ax, 0600h
    mov bx, 0700h
    mov cx, 0
    mov dx, 184fh
    int 10h

    ; 显示字符串"1 MBR"
    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xA4

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

                                    ; 读入Loader的起始扇区号和写入的地址
    mov eax, LOADER_START_SECTOR    ; 起始扇区号 LBA
    mov bx, LOADER_BASE_ADDR        ; 写入的地址
    mov cx, 4                       ; 待写入的扇区数
    call rd_disk_m_16               ; 读n个扇区, cx是参数n

    jmp LOADER_BASE_ADDR            ; 跳到Loader所在地址, 执行Loader

; 读n个扇区, n存在cx里, LBA扇区号存在eax里
rd_disk_m_16:
    ; 备份eax, cx
    mov esi, eax
    mov di, cx

    ; 设置要读取的扇区数
    mov dx, 0x1f2
    mov al, cl
    out dx, al      ; out用于向外设发送数据
    mov eax, esi    ; 恢复ax

    ; 根据表3-17, 把LBA地址填到0x1f3-0x1f6
    ; LBA有两种, LBA28和LBA48, 2^28个扇区是128GB, 2^48个扇区是128PB (每个扇区512B)
    mov dx, 0x1f3   ; 7~0位写入0x1f3
    out dx, al

    mov cl, 8       ; 15~8位写入0x1f4
    shr eax, cl
    mov dx, 0x1f4
    out dx, al

    shr eax, cl     ; 23~16位写入0x1f5
    mov dx, 0x1f5
    out dx, al

    shr eax, cl     ; 27~24位, 1110, 表示LBA模式
    and al, 0x0f
    or al, 0xe0
    mov dx, 0x1f6
    out dx, al

    ; 书里主要用三个命令: 0xEC硬盘识别, 0x20读扇区, 0x30写扇区
    ; 根据表3-17, 需要向端口0x1f7写入命令0x20, 表示读扇区
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

    ; 检查硬盘状态
.not_ready:
    nop 
    in al, dx
    and al, 0x88
    cmp al, 0x08
    jnz .not_ready

    ; 从端口0x1f0读数据
    mov ax, di
    mov dx, 256         ; 计算需要读取的次数, 一个扇区512B
    mul dx              ; 每次读入一个word(2B), 读256次
    mov cx, ax          ; ax存放着mul的结果, 存到cx是因为loop通过cx来控制
    mov dx, 0x1f0       ; 根据表3-17, 读出来的数据存在0x1f0

.go_on_read:
    in ax, dx
    mov [bx], ax        ; bx是要写入的地址
    add bx, 2           ; 写入2B, 所以加2
    loop .go_on_read
    ret

    times 510-($-$$) db 0
    db 0x55, 0xaa