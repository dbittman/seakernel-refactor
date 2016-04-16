#pragma once
#include <stdint.h>
sysret_t sys_fork(void *, size_t);
intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off);
sysret_t sys_execve(const char *path, char **arg, char **env);
_Noreturn void sys_exit(int);
long sys_gettid(void);
long sys_arch_prctl(int code, unsigned long addr);

sysret_t sys_setresgid(int rid, int eid, int sid);
sysret_t sys_setresuid(int rid, int eid, int sid);
sysret_t sys_setgid(int id);
sysret_t sys_setuid(int id);
sysret_t sys_getegid(void);
sysret_t sys_geteuid(void);
sysret_t sys_getgid(void);
sysret_t sys_getuid(void);
sysret_t sys_getppid(void);
sysret_t sys_getpid(void);

sysret_t sys_fcntl(int fd, int cmd, long arg);
sysret_t sys_ioctl(int fd, unsigned long cmd, long arg);
