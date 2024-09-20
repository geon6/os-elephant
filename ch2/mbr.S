SECTION MBR vstart=0x7c00
	mov ax,0x0000	
	mov ss,ax
	mov ax,0x7c00
	mov sp,ax	
 
	mov ax,0x0600
	mov bx,0x0700	
	mov cx,0x0000
	mov dx,0x184f	
	int 0x10
	
	mov ax,0x0300	
	mov bx,0x0000	
	int 0x10
	
	mov ax,0x0000
	mov es,ax
	mov ax,message
	mov bp,ax
	mov ax,0x1301
	mov bx,0x0007	
	mov cx,0x000c
	int 0x10
	
	jmp $
	message db "Hello World!"

	
	times 510-($-$$) db 0 ; 用0填满当前字节到510字节
	db 0x55,0xaa ; 最后两个字节
