# nasm
nasm -I boot/include/ -o build/mbr.bin boot/mbr.S
nasm -I boot/include/ -o build/loader.bin boot/loader.S
nasm -f elf -o build/print.o lib/kernel/print.S
nasm -f elf -o build/kernel.o kernel/kernel.S

# gcc
gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -fno-stack-protector -o build/main.o kernel/main.c
gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -fno-stack-protector -o build/interrupt.o kernel/interrupt.c
gcc -m32 -I lib/kernel/ -I lib/ -I kernel/ -c -fno-builtin -fno-stack-protector -o build/init.o kernel/init.c

# ld
ld -m elf_i386 -Ttext 0xc0001500 -e main -o build/kernel.bin build/main.o build/init.o build/interrupt.o build/print.o build/kernel.o

# dd
dd if=./build/kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc
