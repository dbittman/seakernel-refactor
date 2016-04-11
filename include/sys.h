#pragma once
#include <stdint.h>
int sys_fork(uintptr_t caller, uintptr_t sp);
intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off);
int sys_execve(const char *path, char **arg, char **env);
void _Noreturn sys_exit(int);
