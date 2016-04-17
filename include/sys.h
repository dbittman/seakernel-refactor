#pragma once
#include <stdint.h>
#include <signal.h>
#include <file.h>
sysret_t sys_fork(void *, size_t);
intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off);
sysret_t sys_execve(const char *path, char **arg, char **env);
void sys_exit(int code);
_Noreturn void sys_do_exit(int code);
long sys_gettid(void);
long sys_arch_prctl(int code, unsigned long addr);

struct sigaction;
sysret_t sys_sigaction(int sig, const struct sigaction *act, struct sigaction *old);
sysret_t sys_kill(int pid, int sig);
sysret_t sys_sigprocmask(int how, const sigset_t *set, sigset_t *oset);

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

struct timespec;
sysret_t sys_nanosleep(const struct timespec *req, struct timespec *rem);

struct timespec;
struct timeval;
sysret_t sys_pselect(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask);
sysret_t sys_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, struct timeval *timeout);

struct itimerval;
sysret_t sys_getitimer(int which, struct itimerval *cur);
sysret_t sys_setitimer(int which, const struct itimerval *new, struct itimerval *old);

