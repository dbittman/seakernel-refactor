#include <thread.h>
#include <processor.h>
#include <interrupt.h>
#include <printk.h>

_Atomic long long min_time = 0;

static struct thread *__select_thread(struct processor *proc)
{
	/* throw the old process back on the queue */
	if(proc->running->state == THREADSTATE_RUNNING && proc->running != &proc->idle_thread) {
		if(!(atomic_fetch_or(&proc->running->flags, THREAD_ONQUEUE) & THREAD_ONQUEUE))
			priqueue_insert(&proc->runqueue, &proc->running->runqueue_node,
					proc->running, thread_current_priority(proc->running));
	}
	struct thread *thread = priqueue_pop(&proc->runqueue);
	if(!thread)
		thread = &proc->idle_thread;
	else
		thread->flags &= ~THREAD_ONQUEUE;
	proc->running = thread;
	return thread;
}

static void __do_schedule(int save_preempt)
{
	int old = arch_interrupt_set(0);
	struct processor *curproc = processor_get_current();
	struct workqueue *wq = &curproc->workqueue;
	int preempt_old = curproc->preempt_disable - 1 /* -1 for the handle of curproc we hold */;
	if(!save_preempt && curproc->preempt_disable > 1) {
		processor_release(curproc);
		arch_interrupt_set(old);
		return;
	} else {
		curproc->preempt_disable = 1;
	}
	
	struct thread *next = __select_thread(curproc);
	processor_release(curproc);
	current_thread->flags &= ~THREAD_RESCHEDULE;
	if(next != current_thread) {
		//printk(":%d: %ld -> %ld\n", curproc->id, current_thread->tid, next->tid);
		arch_thread_context_switch(current_thread, next);
	}

	if(save_preempt) {
		/* we're playing fast-and-loose here with references. We know that we'll be
		 * fine since we've disabled interrupts, so we can technically drop the reference
		 * to curproc before we get here... uhg */
		curproc->preempt_disable = preempt_old;
	}

	arch_interrupt_set(old);
	/* threads have to do some kernel work! */
	if(!save_preempt && !workqueue_empty(wq)) {
		workqueue_execute(wq);
	}

}

void schedule()
{
	__do_schedule(1);
}

void preempt()
{
	__do_schedule(0);
}

