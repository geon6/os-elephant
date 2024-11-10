BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIB = -I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ \
	-I device/ -I thread/ -I userprog/ -I fs/ -I shell/
ASFLAGS = -f elf
ASBINLIB = -I boot/include/
CFLAGS = -m32 -fno-stack-protector -Wall $(LIB) -c -fno-builtin -W -Wstrict-prototypes -Wmissing-prototypes
LDFLAGS = -m elf_i386 -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
	$(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
	$(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
	$(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
	$(BUILD_DIR)/switch.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/console.o \
	$(BUILD_DIR)/keyboard.o $(BUILD_DIR)/ioqueue.o $(BUILD_DIR)/tss.o \
	$(BUILD_DIR)/process.o $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall-init.o \
	$(BUILD_DIR)/stdio.o $(BUILD_DIR)/ide.o $(BUILD_DIR)/stdio-kernel.o \
	$(BUILD_DIR)/fs.o $(BUILD_DIR)/inode.o $(BUILD_DIR)/file.o \
	$(BUILD_DIR)/dir.o $(BUILD_DIR)/fork.o $(BUILD_DIR)/shell.o \
	$(BUILD_DIR)/assert.o $(BUILD_DIR)/builtin_cmd.o

boot: $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin
# mbr
$(BUILD_DIR)/mbr.bin: boot/mbr.S
	$(AS) $(ASBINLIB) $< -o $@

# bootloader
$(BUILD_DIR)/loader.bin: boot/loader.S
	$(AS) $(ASBINLIB) $< -o $@

# c
$(BUILD_DIR)/main.o: kernel/main.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: kernel/debug.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: thread/sync.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/console.o: device/console.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/keyboard.o: device/keyboard.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ioqueue.o: device/ioqueue.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/tss.o: userprog/tss.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/process.o: userprog/process.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall.o: lib/user/syscall.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/syscall-init.o: userprog/syscall-init.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio.o: lib/stdio.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ide.o: device/ide.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/stdio-kernel.o: lib/kernel/stdio-kernel.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/fs.o: fs/fs.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/inode.o: fs/inode.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/file.o: fs/file.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/dir.o: fs/dir.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/fork.o: userprog/fork.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/shell.o: shell/shell.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/assert.o: lib/user/assert.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/builtin_cmd.o: shell/builtin_cmd.c
	$(CC) $(CFLAGS) $< -o $@

# asm
$(BUILD_DIR)/kernel.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/print.o: lib/kernel/print.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/switch.o: thread/switch.S
	$(AS) $(ASFLAGS) $< -o $@

# ld
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

.PHONY : mk_dir hd clean build all boot

mk_dir:
	if [[! -d $(BUILD_DIR) ]]; then mkdir $(BUILD_DIR); fi

hd: 
	dd if=$(BUILD_DIR)/mbr.bin of=/usr/local/bochs/hd60M.img bs=512 count=1 conv=notrunc && \
	dd if=$(BUILD_DIR)/loader.bin of=/usr/local/bochs/hd60M.img bs=512 count=4 seek=2 conv=notrunc && \
	dd if=$(BUILD_DIR)/kernel.bin of=/usr/local/bochs/hd60M.img bs=512 count=200 seek=9 conv=notrunc

clean:
	cd $(BUILD_DIR) && rm -f ./*

build: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin

all: mk_dir boot build hd