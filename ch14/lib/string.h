#ifndef __LIB_STRING_H
#define __LIB_STRING_H

#include "stdint.h"

// ----- mem ------

void memset(void* dst_, uint8_t value, uint32_t size);

void memcpy(void* dst_, const void* src_, uint32_t size);

int memcmp(const void* a_, const void* b_, uint32_t size);

// ----- str ------

char* strcpy(char* dst_, const char* src_);

uint32_t strlen(const char* str);

int8_t strcmp(const char* a, const char* b);

//从左到右找ch第一次在str中出现的位置
char* strchr(const char* str, const uint8_t ch);

char* strrchr(const char* str, const uint8_t ch);

// src拼接到dst后面, 返回值是dst
char* strcat(char* dst_, const char* src_);

// 在str中找ch出现的次数
uint32_t strchrs(const char* str, uint8_t ch);

#endif
