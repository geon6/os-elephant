; 功能: 闪烁字符, 背景色, 前景色
SECTION MBR vstart=0x7c00
    ; 用cs初始化其他寄存器, 因为BIOS通过 jmp 0:0x7c00 跳到MBR
    ; 所以cs为0. 段寄存器(ds, es...)不能用imm初始化, 借助ax进行初始化
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov gs, ax

    ; 清屏
    ; 用 int 0x10的0x06号功能, 上卷全部行, 等价于清屏
    mov ax, 0600h   ; ah存放功能号, al表示作用的行数, 0表示所有行
    mov bx, 0700h   ; bh为07, 指定功能为上卷行
    mov cx, 0       ; 左上角的坐标(cl, ch)
    mov dx, 184fh   ; 右下角的坐标(dl, dh)
    int 10h

    ; 显示字符串"1 MBR"
    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xA4    ; A表示绿色背景闪烁, 4表示前景色为红色

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

    jmp $

    times 510-($-$$) db 0
    db 0x55, 0xaa