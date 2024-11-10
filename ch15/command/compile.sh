BIN="prog_no_arg"
CFLAGS="-Wall -m32 -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes -Wsystem-headers"
LIB="../lib/"
OBJS="../build/string.o ../build/syscall.o ../build/stdio.o ../build/debug.o ../build/print.o"
DD_IN=$BIN
DD_OUT="/usr/local/bochs/hd60M.img" 

gcc $CFLAGS -I $LIB -o $BIN".o" $BIN".c"
ld -m elf_i386 -Ttext 0x8060000 -e main $BIN".o" $OBJS -o $BIN
SEC_CNT=$(ls -l $BIN | awk '{printf("%d", ($5+511)/512)}')
dd if=./$DD_IN of=$DD_OUT bs=512 count=$SEC_CNT seek=300 conv=notrunc
