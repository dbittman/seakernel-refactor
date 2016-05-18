#ifndef __PROCESSOR_H
#define __PROCESSOR_H

#define MAX_PROCESSORS 64

#define PROCESSOR_PRESENT 1
#define PROCESSOR_UP      2


#include <thread.h>
#include <workqueue.h>
#include <lib/linkedlist.h>
#include <arch-processor.h>
#include <priqueue.h>
#include <interrupt.h>

struct processor {
	struct arch_processor arch;
	int id;
	int flags;
	struct thread idle_thread;
	_Atomic long long time;
	_Atomic int preempt_disable;
	void *idle_stack;
	struct workqueue workqueue;
	struct priqueue runqueue;
	struct thread * _Atomic running;
	struct spinlock schedlock;
};

void processor_create(int id, int flags);
void arch_processor_poke_secondary(int id, uintptr_t);
void arch_processor_reset(void);
void processor_start_secondaries(void);
void processor_secondary_main(void);
int arch_processor_current_id(void);
void processor_add_thread(struct processor *proc, struct thread *thread);
void processor_remove_thread(struct thread *thread);

extern struct processor plist[];

static inline struct processor *processor_get_id(int id)
{
	return &plist[id];
}

static inline struct processor *processor_get_current(void)
{
	int old = arch_interrupt_set(0);
	struct processor *proc = current_thread ? current_thread->processor : processor_get_id(arch_processor_current_id());
	int r = atomic_fetch_add_explicit(&proc->preempt_disable, 1, memory_order_acquire);
	assert(r >= 0);
	arch_interrupt_set(old);
	return proc;
}

static inline void processor_release(struct processor *proc)
{
	int r = atomic_fetch_sub_explicit(&proc->preempt_disable, 1, memory_order_release);
	assert(r > 0);
}

static inline void processor_disable_preempt(void)
{
	int old = arch_interrupt_set(0);
	struct processor *proc = current_thread ? current_thread->processor : processor_get_id(arch_processor_current_id());
	int r = atomic_fetch_add_explicit(&proc->preempt_disable, 1, memory_order_acquire);
	assert(r >= 0);
	arch_interrupt_set(old);
}

static inline void processor_enable_preempt(void)
{
	int old = arch_interrupt_set(0);
	struct processor *proc = current_thread ? current_thread->processor : processor_get_id(arch_processor_current_id());
	int r = atomic_fetch_sub_explicit(&proc->preempt_disable, 1, memory_order_release);
	assert(r > 0);
	arch_interrupt_set(old);
}

#if FEATURE_SUPPORTED_CYCLE_COUNT
uint64_t arch_processor_get_cycle_count(void);
#endif
uint64_t arch_processor_get_nanoseconds(void);

#endif

