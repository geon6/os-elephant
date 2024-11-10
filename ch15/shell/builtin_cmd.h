#ifndef __SHELL_BUILTIN_CMD_H
#define __SHELL_BUILTIN_CMD_H

#include "stdint.h"

void make_clear_abs_path(char* path, char* wash_buf);
void builtin_ls(uint32_t argc, char** argv);
char* builtin_cd(uint32_t argc, char** argv);
int32_t builtin_mkdir(uint32_t argc, char** argv);
int32_t builtin_rmdir(uint32_t argc, char** argv);
int32_t builtin_rm(uint32_t argc, char** argv);
void builtin_pwd(uint32_t argc, char** argv);
void builtin_ps(uint32_t argc, char** argv);
void builtin_clear(uint32_t argc, char** argv);

#endif
