#pragma once
#include <stddef.h>
size_t strlen(const char *s);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memchr(const void* ptr, int value, size_t num);
int strcmp(const char* str1, const char* str2);
void *memset(void *s, int c, size_t n);
int strncmp(const char* s1, const char* s2, size_t n);
void* memcpy(void* destination, const void* source, size_t num);
void* memmove(void* destination, const void* source, size_t num);
char *strncpy(char *dest, const char *src, size_t n);
const char *strrchrc(const char *str, char v);
const char *strchrc(const char *str, char v);
long strtol(char *str, char **end, int base);
char *strcat(char *str, const char *cat);
