#pragma once
#include <stdint.h>
#include <signal.h>
#include <file.h>
sysret_t sys_fork(void *);

struct pt_regs;
long sys_clone(unsigned long flags, void *child_stack, void *ptid, void *ctid, struct pt_regs *regs, void*);
intptr_t sys_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, size_t off);
void *sys_mremap(void *old, size_t oldsz, size_t newsz, int flags, void *new);
sysret_t sys_munmap(void *addr, size_t len);
sysret_t sys_mprotect(void *addr, size_t len, int prot);
sysret_t sys_execve(const char *path, char **arg, char **env);
void sys_exit(int code);
void sys_exit_group(int code);
_Noreturn void sys_do_exit(int code);
long sys_gettid(void);
long sys_arch_prctl(int code, unsigned long addr);
uintptr_t sys_brk(void *nb);

struct rusage;
sysret_t sys_wait4(int pid, int *status, int options, struct rusage *usage);

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
sysret_t sys_setgroups(size_t size, const int *list);

sysret_t sys_fcntl(int fd, int cmd, long arg);
sysret_t sys_ioctl(int fd, unsigned long cmd, long arg);

struct timespec;
sysret_t sys_nanosleep(const struct timespec *req, struct timespec *rem);

struct timespec;
struct timeval;
sysret_t sys_utimes(const char *filename, const struct timeval times[2]);
sysret_t sys_pselect(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, const struct timespec *timeout, const sigset_t *sigmask);
sysret_t sys_select(int nfds, fd_set *readfds, fd_set *writefds,
		fd_set *errfds, struct timeval *timeout);

struct itimerval;
sysret_t sys_getitimer(int which, struct itimerval *cur);
sysret_t sys_setitimer(int which, const struct itimerval *new, struct itimerval *old);

sysret_t sys_clock_gettime(int id, struct timespec *res);
sysret_t sys_clock_getres(int id, struct timespec *res);
sysret_t sys_utimensat(int dirfd, const char *filename, const struct timespec times[2]);

sysret_t sys_fsync(int);
sysret_t sys_sync(void);


int sys_setpgid(int pid, int pg);
int sys_setsid(void);

struct utsname;
int sys_uname(struct utsname *buf);



#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_REQUEUE 3
#define FUTEX_PRIVATE 128
sysret_t sys_futex(_Atomic int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3);
long sys_set_tid_address(_Atomic int *addr);

