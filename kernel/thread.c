#include <printk.h>
#include <thread.h>
#include <mmu.h>
#include <stdatomic.h>
#include <processor.h>
#include <slab.h>
#include <thread-bits.h>
#include <lib/hash.h>
#include <system.h>
#include <signal.h>
#include <process.h>
static _Atomic int threadid = ATOMIC_VAR_INIT(0);

static struct kobj_idmap active_threads;

static void _thread_wi_delete(void *obj)
{
	kobj_putref(obj);
}

static void _thread_init(void *obj)
{
	struct thread *thread = obj;
	/* TODO (question) [dbittman]: re-use thread IDs? */
	thread->tid = atomic_fetch_add(&threadid, 1);
	thread->ctx = &kernel_context;
	thread->state = THREADSTATE_INIT;
	thread->time = min_time;
	thread->wi_delete.fn = _thread_wi_delete;
	thread->wi_delete.arg = thread;
	kobj_idmap_insert(&active_threads, thread, &thread->tid);
	thread->process = kernel_process;
	sigemptyset(&thread->pending_signals);
	sigemptyset(&thread->sigmask);
	thread->flags = 0;
	memset(thread->timers, 0, sizeof(thread->timers));
}

static void _thread_create(void *obj)
{
	struct thread *thread = obj;
	spinlock_create(&thread->signal_lock);
	linkedlist_create(&thread->saved_exception_frames, 0);
	thread->kernel_tls_base = (void *)mm_virtual_allocate(KERNEL_STACK_SIZE, false);

	_thread_init(obj);
}

static void _thread_put(void *obj)
{
	struct thread *thread = obj;
	assert(thread != &thread->processor->idle_thread);
	printk("Thread put %ld\n", thread->tid);
	if(thread->ctx != &kernel_context) {
		kobj_putref(thread->ctx);
		thread->ctx = NULL;
	}
}

static void _thread_destroy(void *obj)
{
	struct thread *thread = obj;
	mm_virtual_deallocate((uintptr_t)thread->kernel_tls_base);
}

struct thread *thread_get_byid(unsigned long id)
{
	return kobj_idmap_lookup(&active_threads, &id);
}

void thread_prepare_sleep(void)
{
	processor_disable_preempt();
	assert(current_thread->processor->preempt_disable > 0);
	current_thread->processor->running = &current_thread->processor->idle_thread;
	current_thread->state = THREADSTATE_BLOCKED;
}

void thread_wakeup(void)
{
	current_thread->state = THREADSTATE_RUNNING;
	current_thread->processor->running = current_thread;
	processor_enable_preempt();
}

int thread_current_priority(struct thread *thr)
{
	struct processor *proc = thr->processor;
	if(!proc)
		return 0;
	long long time = thr->time;
	if(proc->time < time || proc->time == 0) {
		proc->time = time;
		return 0;
	}
	return (-(MAX_THREAD_PRIORITY * time) / proc->time) + MAX_THREAD_PRIORITY;
}

_Noreturn void thread_exit(struct thread *thread)
{
	assert(thread == current_thread);
	kobj_idmap_delete(&active_threads, thread, &thread->tid);
	processor_disable_preempt();
	for(int i=0;i<3;i++) {
		if(atomic_exchange(&thread->timers[i].sig, 0)) {
			timer_remove(&thread->timers[i].timer);
		}
	}
	thread->flags |= THREAD_UNINTER;
	thread->state = THREADSTATE_INIT;
	thread->processor->running = &thread->processor->idle_thread;
	workqueue_insert(&thread->processor->workqueue, &thread->wi_delete);
	printk("thread %ld exited\n", thread->tid);
	schedule();
	__builtin_unreachable();
}

struct kobj kobj_thread = {
	KOBJ_DEFAULT_ELEM(thread),
	.create = _thread_create,
	.init = _thread_init,
	.put = _thread_put,
	.destroy = _thread_destroy,
};

__initializer static void thread_idmap_init(void)
{
	struct thread t;
	kobj_idmap_create(&active_threads, sizeof(t.tid));
}

void thread_init(void)
{
	struct processor *proc = processor_get_current();
	proc->idle_thread.kernel_tls_base = proc->idle_stack;
	proc->idle_thread.ctx = &kernel_context;
	proc->idle_thread.tid = threadid++;
	proc->idle_thread.processor = proc;
	proc->idle_thread.process = kernel_process;
	arch_thread_init(&proc->idle_thread);
	processor_release(proc);
}

