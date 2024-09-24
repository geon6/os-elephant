# 常用的命令
```bash
# 我的bochs地址, 版本为 2.6.8
/usr/local/bochs

# nasm编译
nasm -o mbr.bin mbr.S
nasm -I include/ -o mbr.bin mbr.S 
nasm -I include/ -o loader.bin loader.S

# dd 用来把数据写入到虚拟硬盘
sudo dd if=./mbr.bin of=/usr/local/bochs/hd60M.img bs=512 count=1 conv=notrunc
sudo dd if=./loader.bin of=/usr/local/bochs/hd60M.img bs=512 count=4 conv=notrunc seek=2
sudo dd if=./kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc

sudo gcc -c -o main.o main.c && ld main.o -Ttext 0xc0001500 -e main -o kernel.bin && dd if=kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc
```

# chapter 6 编译命令
作者环境与我不同, 他电脑可能是32位的.

nasm产生32位的.o文件, 而gcc产生64位的.o文件, 导致书上的编译命令不对.

安装一个包, 让gcc能产生32位文件
```
sudo apt-get install gcc-multilib
```

添加一些编译和链接的命令参数
```
nasm -f elf -o lib/kernel/print.o lib/kernel/print.S
gcc -m32 -I lib/kernel/ -c -o kernel/main.o kernel/main.c
ld -m elf_i386 -Ttext 0xc0001500 -e main -o kernel/kernel.bin kernel/main.o lib/kernel/print.o
```