#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "fs.h"

int main(void) {
    put_str("i am kernel\n");
    init_all();
    // uint32_t fd = sys_open("/file1", O_RDWR);
    // printf("fd:%d\n", fd);
    // sys_write(fd, "helldasdasdasdasdqweqwo,world\nadioawoihedqowiheoqwheoqwiuda\n", 80);
    // sys_close(fd);
    // printf("%d closed now\n", fd);
    
    uint32_t fd = sys_open("/file1", O_RDWR);
    printf("open /file1, fd:%d\n", fd);
    char buf[64] = {0};
    int read_bytes = sys_read(fd, buf, 18);
    printf("1_ read %d bytes:\n%s\n", read_bytes, buf);

    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("2_ read %d bytes:\n%s", read_bytes, buf);

    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 6);
    printf("3_ read %d bytes:\n%s", read_bytes, buf);

    printf("________  close file1 and reopen  ________\n");
    sys_close(fd);
    fd = sys_open("/file1", O_RDWR);
    memset(buf, 0, 64);
    read_bytes = sys_read(fd, buf, 24);
    printf("4_ read %d bytes:\n%s", read_bytes, buf);

    sys_close(fd);
    while(1);
}
