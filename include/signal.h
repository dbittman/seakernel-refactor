#pragma once

typedef struct __sigset_t { unsigned long __bits[128/sizeof(long)]; } sigset_t;

struct sigaction {
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned mask[2];
};

#define SIG_ERR  ((void (*)(int))-1)
#define SIG_DFL  ((void (*)(int)) 0)
#define SIG_IGN  ((void (*)(int)) 1)

#define signal_is_userspace_handler(h) (h != SIG_ERR && h != SIG_DFL && h != SIG_IGN)

#define SA_NOCLDSTOP  1
#define SA_NOCLDWAIT  2
#define SA_SIGINFO    4
#define SA_ONSTACK    0x08000000
#define SA_RESTART    0x10000000
#define SA_NODEFER    0x40000000
#define SA_RESETHAND  0x80000000
#define SA_RESTORER   0x04000000

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGIOT    SIGABRT
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPOLL   29
#define SIGPWR    30
#define SIGSYS    31
#define SIGUNUSED SIGSYS

#define _NSIG 65

#include <string.h>
#include <assert.h>

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
static inline void sigaddset(sigset_t *set, int sig)
{
    unsigned s = sig-1;
    if (s >= _NSIG-1 || sig-32U < 3) {
    	return;
    }
    set->__bits[s/8/sizeof *set->__bits] |= 1UL<<(s&8*sizeof *set->__bits-1);
    return;
}

static inline void sigdelset(sigset_t *set, int sig)
{
    unsigned s = sig-1;
    if (s >= _NSIG-1 || sig-32U < 3) {
    	return;
    }
    set->__bits[s/8/sizeof *set->__bits] &=~(1UL<<(s&8*sizeof *set->__bits-1));
    return;
}

static inline bool sigismember(const sigset_t *set, int sig)
{
    unsigned s = sig-1;
    if (s >= _NSIG-1) return false;
    return !!(set->__bits[s/8/sizeof *set->__bits] & 1UL<<(s&8*sizeof *set->__bits-1));
}

static inline bool sigisemptyset(const sigset_t *set)
{
	static const unsigned long zeroset[_NSIG/8/sizeof(long)];
	return !memcmp(set, &zeroset, _NSIG/8);
}

static inline void sigemptyset(sigset_t *set)
{
	set->__bits[0] = 0;
	if (sizeof(long)==4 || _NSIG > 65) set->__bits[1] = 0;
	if (sizeof(long)==4 && _NSIG > 65) {
	    set->__bits[2] = 0;
	    set->__bits[3] = 0;
	}
}

#define SST_SIZE (_NSIG/8/sizeof(long))
static inline int sigorset(sigset_t *dest, const sigset_t *left, const sigset_t *right)
{
    unsigned long i = 0, *d = (void*) dest, *l = (void*) left, *r = (void*) right;
    for(; i < SST_SIZE; i++) d[i] = l[i] | r[i];
    return 0;
}

#pragma GCC diagnostic pop
