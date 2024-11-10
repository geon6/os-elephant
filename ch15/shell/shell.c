#include "shell.h"
#include "assert.h"
#include "file.h"
#include "fs.h"
#include "global.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define cmd_len 128    // 最大支持键入128个字符的命令行输入
#define MAX_ARG_NR 16  // 加上命令名外,最多支持15个参数

// 存储输入的命令
static char cmd_line[cmd_len] = {0};
char final_path[MAX_PATH_LEN] = {0};

// 用来记录当前目录,是当前目录的缓存,每次执行cd命令时会更新此内容
char cwd_cache[64] = {0};

// 输出提示符
void print_prompt() {
    printf("[geon6@localhost %s]$ ", cwd_cache);
}

// 从键盘缓冲区中最多读入count个字节到buf
static void readline(char* buf, int32_t count) {
    assert(buf != NULL && count > 0);
    char* pos = buf;

    while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
        // 在不出错情况下,直到找到回车符才返回
        switch (*pos) {
            // 找到回车或换行符后认为键入的命令结束,直接返回
            case '\n':
            case '\r':
                *pos = 0;  // 添加cmd_line的终止字符0
                putchar('\n');
                return;

            case '\b':
                if (buf[0] != '\b') {  // 阻止删除非本次输入的信息
                    --pos;  // 退回到缓冲区cmd_line中上一个字符
                    putchar('\b');
                }
                break;

            case 'l' - 'a':  // ctrl l
                *pos = 0;
                clear();
                print_prompt();
                printf("%s", buf);
                break;

            case 'u' - 'a':  // ctrl u
                while (buf != pos) {
                    putchar('\b');
                    *(pos--) = 0;
                }
                break;
            // 非控制键则输出字符
            default: putchar(*pos); pos++;
        }
    }
    printf("readline: can`t find enter_key in the cmd_line, max num of char is "
           "128\n");
}

// 用token对cmd_str进行分割, 结果存入argv, 返回argc, 失败返回-1
static int32_t cmd_parse(char* cmd_str, char** argv, char token) {
    assert(cmd_str != NULL);
    int32_t arg_idx = 0;
    while (arg_idx < MAX_ARG_NR) {
        argv[arg_idx] = NULL;
        arg_idx++;
    }
    char* next = cmd_str;
    int32_t argc = 0;
    while (*next) {
        while (*next == token) { next++; }
        if (*next == 0) { break; }
        argv[argc] = next;
        while (*next && &next != token) { next++; }
        if (*next) { *next++ = 0; }
        if (argc > MAX_ARG_NR) { return -1; }
        argc++;
    }
    return argc;
}

char* argv[MAX_ARG_NR];  // 为了exec能够访问参数, 设置为全局变量
int32_t argc = -1;

// 简单的shell
void my_shell() {
    cwd_cache[0] = '/';
    while (1) {
        print_prompt();
        memset(final_path, 0, MAX_PATH_LEN);
        memset(cmd_line, 0, MAX_PATH_LEN);
        readline(cmd_line, MAX_PATH_LEN);
        if (cmd_line[0] == 0) {  // 若只键入了一个回车
            continue;
        }
        argc = -1;
        argc = cmd_parse(cmd_line, argv, ' ');
        if (argc == -1) {
            printf("num of arguments exceed %d\n", MAX_ARG_NR);
            continue;
        }
        if (!strcmp("ls", argv[0])) {
            builtin_ls(argc, argv);
        } else if (!strcmp("cd", argv[0])) {
            if (builtin_cd(argc, argv) != NULL) {
                memset(cwd_cache, 0, MAX_PATH_LEN);
                strcpy(cwd_cache, final_path);
            }
        } else if (!strcmp("pwd", argv[0])) {
            builtin_pwd(argc, argv);
        } else if (!strcmp("ps", argv[0])) {
            builtin_ps(argc, argv);
        } else if (!strcmp("clear", argv[0])) {
            builtin_clear(argc, argv);
        } else if (!strcmp("mkdir", argv[0])) {
            builtin_mkdir(argc, argv);
        } else if (!strcmp("rmdir", argv[0])) {
            builtin_rmdir(argc, argv);
        } else if (!strcmp("rm", argv[0])) {
            builtin_rm(argc, argv);
        } else {
            printf("external command\n");
        }
    }
    panic("my_shell: should not be here");
}
