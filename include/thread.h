#ifndef __THREAD_H
#define __THREAD_H
#include <stdint.h>
#include <stdbool.h>
#include <arch-thread.h>
#include <mmu.h>
#include <slab.h>
#include <assert.h>
#include <lib/linkedlist.h>
#include <workqueue.h>
#include <slab.h>
#include <timer.h>
#include <priqueue.h>
#include <signal.h>
#define THREAD_RESCHEDULE 1
#define THREAD_EXIT 2
#define THREAD_ONQUEUE 4
#define THREAD_UNINTER 8
enum thread_state {
	THREADSTATE_INIT,
	THREADSTATE_RUNNING,
	THREADSTATE_BLOCKED,
};

struct itimerval {
	struct timeval it_interval; /* Interval for periodic timer */
	struct timeval it_value;    /* Time until next expiration */
};

struct thread_timer {
	struct timeval interval;
	struct timeval value;
	struct thread *thread;
	struct timer timer;
	_Atomic int sig;
};
#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct process;
struct vm_context;
struct processor;
struct thread {
	struct kobj_header _header;
	struct vm_context *ctx;
	void *kernel_tls_base, *stackpointer;
	void *user_tls_base;
	unsigned long tid;
	/* A */ long long time /* ago, I can still remember when that music... */;
	_Atomic int flags;
	_Atomic enum thread_state state;

	struct priqueue_node runqueue_node;
	struct workitem wi_delete;

	struct arch_thread arch;
	struct processor * _Atomic processor;

	struct process *process;
	struct linkedentry proc_entry;
	
	struct spinlock signal_lock;
	sigset_t pending_signals, sigmask;
	_Atomic int signal;
	int exit_code;

	struct linkedlist saved_exception_frames;
	struct thread_timer timers[3];


#if CONFIG_DEBUG
	ssize_t held_spinlocks;
#endif
};

#define current_thread arch_thread_get_current()
#define current_context (current_thread ? current_thread->ctx : &kernel_context)

bool thread_check_status_retuser(struct thread *thread);
void arch_thread_fork_entry(void *);
void arch_thread_context_switch(struct thread *, struct thread *thr);
void arch_thread_usermode_jump(uintptr_t entry, uintptr_t initial_stack);
void thread_create(struct thread *, struct vm_context *ctx, uintptr_t entry);
void arch_thread_create(struct thread *next, uintptr_t entry, void *data);
void arch_thread_init(struct thread *us);
_Noreturn void thread_exit(struct thread *thread);
struct thread *thread_get_byid(unsigned long id);
void thread_wakeup(void);
void thread_prepare_sleep(void);
void thread_init(void);
void preempt(void);
void schedule(void);
void kernel_idle_work(void);
bool thread_send_signal(struct thread *thread, int signal);
static inline bool thread_is_runable(struct thread *thr)
{
	assert(thr != NULL);
	return thr->state == THREADSTATE_RUNNING;
}

extern struct kobj kobj_thread;

extern _Atomic long long min_time;
int thread_current_priority(struct thread *thr);
#define MAX_THREAD_PRIORITY 15 /* [0 - 15] */
#endif

