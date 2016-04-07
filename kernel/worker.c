#include <thread.h>
#include <worker.h>
#include <slab.h>
#include <processor.h>
#include <printk.h>
void worker_start(struct worker *worker, void (*fn)(struct worker *), void *data)
{
	worker->thread = kobj_allocate(&kobj_thread);
	worker->flags = 0;
	worker->arg = data;
	arch_thread_create(worker->thread, (uintptr_t)fn, worker);
	struct processor *proc = processor_get_current();
	worker->thread->state = THREADSTATE_RUNNING;
	/* inc refs for adding it to the processor */
	kobj_getref(worker->thread);
	processor_add_thread(proc, worker->thread);
	processor_release(proc);
}

void worker_exit(struct worker *w, int code)
{
	w->exitcode = code;
	struct thread *thread = w->thread;
	atomic_fetch_or(&w->flags, WORKER_EXIT);
	kobj_putref(thread);
	thread_exit(thread);
}

bool worker_join(struct worker *w)
{
	atomic_fetch_or(&w->flags, WORKER_JOIN);
	return atomic_load(&w->flags) & WORKER_EXIT;
}

