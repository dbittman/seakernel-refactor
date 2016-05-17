#include <blocklist.h>
#include <thread.h>
#include <processor.h>
#include <printk.h>
#include <trace.h>
#include <system.h>

TRACE_DEFINE(blocking_trace, "blocking");

void blocklist_create(struct blocklist *bl)
{
	linkedlist_create(&bl->waitlist, LINKEDLIST_LOCKLESS);
	spinlock_create(&bl->lock);
}

void thread_unblock(struct thread *thread)
{
	TRACE(&blocking_trace, "unblock thread %ld (%d) %d",
			thread->tid, thread->processor->id, thread->flags & THREAD_UNINTER);
	spinlock_acquire(&thread->processor->schedlock);
	if(!(thread->flags & THREAD_UNINTER)) {
		if(thread->state != THREADSTATE_INIT) {
			thread->state = THREADSTATE_RUNNING;
			processor_add_thread(thread->processor, thread);
		}
	}
	spinlock_release(&thread->processor->schedlock);
}

static void blockpoint_unblock(struct blockpoint *bp)
{
	spinlock_acquire(&bp->thread->processor->schedlock);
	bp->flags |= BLOCK_UNBLOCKED;
	if(bp->thread->state != THREADSTATE_INIT) {
		bp->thread->state = THREADSTATE_RUNNING;
		processor_add_thread(bp->thread->processor, bp->thread);
	}
	spinlock_release(&bp->thread->processor->schedlock);
}

bool blocklist_unblock_one(struct blocklist *bl)
{
	spinlock_acquire(&bl->lock);
	TRACE(&blocking_trace, "unblock one %p", bl);

	struct linkedentry *entry;
	bool success = false;
	for(entry = linkedlist_back_iter_start(&bl->waitlist);
			entry != linkedlist_back_iter_end(&bl->waitlist);
			entry = linkedlist_back_iter_next(entry)) {
		struct blockpoint *bp = linkedentry_obj(entry);
		TRACE(&blocking_trace, "UB1: %p %p", entry, bp);
		if(!(bp->flags & BLOCK_UNBLOCKED)) {
			if(bp->thread->processor == current_thread->processor)
				current_thread->flags |= THREAD_RESCHEDULE;
			blockpoint_unblock(bp);
			success = true;
			break;
		}
	}
	spinlock_release(&bl->lock);
	if(current_thread && (current_thread->flags & THREAD_RESCHEDULE))
		preempt();
	return success;
}

void blocklist_unblock_all(struct blocklist *bl)
{
	spinlock_acquire(&bl->lock);
	TRACE(&blocking_trace, "unblock all %p", bl);

	struct linkedentry *entry;
	for(entry = linkedlist_back_iter_start(&bl->waitlist);
			entry != linkedlist_back_iter_end(&bl->waitlist);
			entry = linkedlist_back_iter_next(entry)) {
		struct blockpoint *bp = linkedentry_obj(entry);
		TRACE(&blocking_trace, "UB: %p %p", entry, bp);
		if(!(bp->flags & BLOCK_UNBLOCKED)) {
			if(bp->thread->processor == current_thread->processor)
				current_thread->flags |= THREAD_RESCHEDULE;
			blockpoint_unblock(bp);
		}
	}
	spinlock_release(&bl->lock);
	if(current_thread && (current_thread->flags & THREAD_RESCHEDULE))
		preempt();
}

static void __timeout(void *data)
{
	struct blockpoint *bp = data;
	bp->result = BLOCK_RESULT_TIMEOUT;
	blockpoint_unblock(bp);
}

#include <processor.h>
void blockpoint_startblock(struct blocklist *bl, struct blockpoint *bp)
{
	assert(bp->bl == NULL);
	processor_disable_preempt();
	assert(current_thread->processor->preempt_disable > 0);
	spinlock_acquire(&bl->lock);
	TRACE(&blocking_trace, "start block %p: %ld, %d, on %p (%x) %d", bp, current_thread->tid, bp->flags & BLOCK_UNINTERRUPT, bl, current_thread->flags, current_thread->signal);
	spinlock_acquire(&current_thread->processor->schedlock);
	if(bp->flags & BLOCK_UNINTERRUPT)
		current_thread->flags |= THREAD_UNINTER;
	else
		current_thread->flags &= ~THREAD_UNINTER;
	assert(current_thread->processor->running == current_thread || current_thread->processor->running == &current_thread->processor->idle_thread);
	current_thread->processor->running = &current_thread->processor->idle_thread;
	current_thread->state = THREADSTATE_BLOCKED;
	spinlock_release(&current_thread->processor->schedlock);
	bp->thread = current_thread;
	bp->bl = bl;
	bp->result = BLOCK_RESULT_BLOCKED;
	linkedlist_insert(&bl->waitlist, &bp->node, bp);
	spinlock_release(&bl->lock);

	if(bp->flags & BLOCK_TIMEOUT)
		timer_add(&bp->timer, TIMER_MODE_ONESHOT, bp->timeout, __timeout, bp);
}

enum block_result blockpoint_cleanup(struct blockpoint *bp)
{
	assert(bp->bl != NULL);
	spinlock_acquire(&bp->bl->lock);
	TRACE(&blocking_trace, "cleanup block: %ld %p on %p", current_thread->tid, bp, bp->bl);
	assert(current_thread->processor->preempt_disable > 0);
	spinlock_acquire(&current_thread->processor->schedlock);
	current_thread->flags &= ~THREAD_UNINTER;
	current_thread->state = THREADSTATE_RUNNING;
	assert(current_thread->processor->running == current_thread || current_thread->processor->running == &current_thread->processor->idle_thread);
	current_thread->processor->running = current_thread;
	spinlock_release(&current_thread->processor->schedlock);
	linkedlist_remove(&bp->bl->waitlist, &bp->node);
	spinlock_release(&bp->bl->lock);
	bp->bl = NULL;
	processor_enable_preempt();

	if(bp->result == BLOCK_RESULT_BLOCKED && (bp->flags & BLOCK_UNBLOCKED))
		bp->result = BLOCK_RESULT_UNBLOCKED;
	if(bp->flags & BLOCK_TIMEOUT)
		timer_remove(&bp->timer);
	if(current_thread->signal && !(bp->flags & BLOCK_UNINTERRUPT))
		bp->result = BLOCK_RESULT_INTERRUPTED;
	return bp->result;
}

__orderedinitializer(__orderedafter(TRACE_INITIALIZER)) static void init_trace(void)
{
	//trace_enable(&blocking_trace);
}

