#include <thread.h>
#include <processor.h>
#include <interrupt.h>
#include <printk.h>

_Atomic long long min_time = 0;

static struct thread *__select_thread(struct processor *proc)
{
	/* throw the old process back on the queue */
	spinlock_acquire(&proc->schedlock);
	if(current_thread->flags & THREAD_DEAD)
		current_thread->flags |= THREAD_GONE;
	if(likely(proc->running->state == THREADSTATE_RUNNING && proc->running != &proc->idle_thread)) {
		if(likely(!(atomic_fetch_or(&proc->running->flags, THREAD_ONQUEUE) & THREAD_ONQUEUE))) {
			assert(proc->running == current_thread && !(current_thread->flags & THREAD_DEAD));
			priqueue_insert(&proc->runqueue, &proc->running->runqueue_node,
					proc->running, thread_current_priority(proc->running));
		}
	}
	struct thread *thread = priqueue_pop(&proc->runqueue);
	if(unlikely(!thread))
		thread = &proc->idle_thread;
	else {
		thread->flags &= ~THREAD_ONQUEUE;
	}
	if(((thread->flags & THREAD_UNINTER) && thread->state != THREADSTATE_RUNNING)
			|| thread->flags & THREAD_DEAD) {
		thread = &proc->idle_thread;
	}

	/* this is a weird edge case (that should probably get fixed up, TODO):
	 * if a thread exits and another thread unblocks that exiting thread (for
	 * example, it gets a signal), then the thread may be added to the runqueue
	 * during its exiting. Threads that are exiting don't "remove" themselves from
	 * the runqueue because that happens in the scheduler above, so they could be
	 * in the runqueue in an unrunnable state. Then, another thread creates a new
	 * thread and the slab allocator returns the recently exited thread. The flags
	 * are cleared and the scheduler is then free to run that "new" thread...with the
	 * old state. Thus allowing the thread to reach the unreachable part of thread_exit.
	 *
	 * So, if a thread's state is INIT, then don't run it. Wait until the creating thread
	 * sets it to runable. */
	if(unlikely(thread->state == THREADSTATE_INIT)) {
		thread = &proc->idle_thread;
	}
	proc->running = thread;
	spinlock_release(&proc->schedlock);
	return thread;
}

static void _check_signals(struct thread *thread)
{
	spinlock_acquire(&thread->signal_lock);

	if(!sigisemptyset(&thread->pending_signals)) {
		for(int i=1;i<_NSIG;i++) {
			if(sigismember(&thread->pending_signals, i)) {
				sigdelset(&thread->pending_signals, i);
				thread->signal = i;
				if(!(thread->flags & THREAD_UNINTER)) {
					thread->state = THREADSTATE_RUNNING;
					thread->processor->running = thread;
				}
				break;
			}
		}
	}

	spinlock_release(&thread->signal_lock);
}

static void __do_schedule(int save_preempt)
{
	int old = arch_interrupt_set(0);
	struct processor *curproc = processor_get_current();
	struct workqueue *wq = &curproc->workqueue;
	int preempt_old = curproc->preempt_disable - 1 /* -1 for the handle of curproc we hold */;
	assert(preempt_old >= 0);
	if(!save_preempt && curproc->preempt_disable > 1) {
		processor_release(curproc);
		arch_interrupt_set(old);
		return;
	} else {
		curproc->preempt_disable = 1;
	}
	
#if CONFIG_DEBUG
	//assert(current_thread->held_spinlocks == 0);
#endif
	_check_signals(current_thread);

	struct thread *next = __select_thread(curproc);
	processor_release(curproc);
	current_thread->flags &= ~THREAD_RESCHEDULE;
	if(next != current_thread) {
		//printk(":%d: %ld -> %ld\n", curproc->id, current_thread->tid, next->tid);
		arch_thread_context_switch(current_thread, next);
		_check_signals(current_thread);
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

