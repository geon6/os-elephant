# 常用的命令
```bash
# 我的bochs地址, 版本为 2.6.8
/usr/local/bochs

# nasm编译
nasm -o mbr.bin mbr.S
nasm -I include/ -o loader.bin loader.S

# dd
sudo dd if=./mbr.bin of=/usr/local/bochs/hd60M.img bs=512 count=1 conv=notrunc
sudo dd if=./loader.bin of=/usr/local/bochs/hd60M.img bs=512 count=4 conv=notrunc seek=2
sudo dd if=./kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc

sudo gcc -c -o main.o main.c && ld main.o -Ttext 0xc0001500 -e main -o kernel.bin && dd if=kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc
```