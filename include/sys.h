#pragma once
#include <stdint.h>
sysret_t sys_fork(void *, size_t);
intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off);
sysret_t sys_execve(const char *path, char **arg, char **env);
_Noreturn void sys_exit(int);
long sys_gettid(void);
long sys_arch_prctl(int code, unsigned long addr);
