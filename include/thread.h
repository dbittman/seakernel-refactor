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
#define THREAD_RESCHEDULE 1
#define THREAD_EXIT 2
enum thread_state {
	THREADSTATE_INIT,
	THREADSTATE_RUNNING,
	THREADSTATE_BLOCKED,
};

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
	int flags;
	_Atomic enum thread_state state;

	struct priqueue_node runqueue_node;
	struct workitem wi_delete;

	struct arch_thread arch;
	struct processor * _Atomic processor;

	struct process *process;
	struct linkedentry proc_entry;
};

#define current_thread arch_thread_get_current()
#define current_context (current_thread ? current_thread->ctx : &kernel_context)

void arch_thread_fork_entry(void *);
void arch_thread_context_switch(struct thread *, struct thread *thr);
void arch_thread_usermode_jump(uintptr_t entry, uintptr_t initial_stack);
void thread_create(struct thread *, struct vm_context *ctx, uintptr_t entry);
void arch_thread_create(struct thread *next, uintptr_t entry, void *data);
void arch_thread_init(struct thread *us);
_Noreturn void thread_exit(struct thread *thread);
struct thread *thread_get_byid(unsigned long id);
void thread_init(void);
void preempt(void);
void schedule(void);
void kernel_idle_work(void);
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

