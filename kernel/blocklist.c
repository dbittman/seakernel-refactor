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
	TRACE(&blocking_trace, "unblock thread %ld (%d)",
			thread->tid, thread->processor->id);
	thread->state = THREADSTATE_RUNNING;
	processor_add_thread(thread->processor, thread);
}

void blockpoint_unblock(struct blockpoint *bp)
{
	bp->flags |= BLOCK_UNBLOCKED;
	thread_unblock(bp->thread);
}

bool blocklist_unblock_one(struct blocklist *bl)
{
	TRACE(&blocking_trace, "unblock one");
	spinlock_acquire(&bl->lock);

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
	TRACE(&blocking_trace, "unblock one");
	spinlock_acquire(&bl->lock);

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
	TRACE(&blocking_trace, "start block: %ld", current_thread->tid);
	processor_disable_preempt();
	assert(current_thread->processor->preempt_disable > 0);
	spinlock_acquire(&bl->lock);
	current_thread->processor->running = &current_thread->processor->idle_thread;
	current_thread->state = THREADSTATE_BLOCKED;
	bp->thread = current_thread;
	bp->bl = bl;
	bp->result = BLOCK_RESULT_BLOCKED;
	TRACE(&blocking_trace, "insert: %p %p", &bp->node, bp);
	linkedlist_insert(&bl->waitlist, &bp->node, bp);
	spinlock_release(&bl->lock);

	if(bp->flags & BLOCK_TIMEOUT)
		timer_add(&bp->timer, TIMER_MODE_ONESHOT, bp->timeout, __timeout, bp);
}

enum block_result blockpoint_cleanup(struct blockpoint *bp)
{
	TRACE(&blocking_trace, "cleanup block: %ld", current_thread->tid);
	assert(bp->bl != NULL);
	assert(current_thread->processor->preempt_disable > 0);
	thread_unblock(current_thread);
	spinlock_acquire(&bp->bl->lock);
	TRACE(&blocking_trace, "remove: %p %p", &bp->node, bp);
	linkedlist_remove(&bp->bl->waitlist, &bp->node);
	spinlock_release(&bp->bl->lock);
	processor_enable_preempt();

	if(bp->result == BLOCK_RESULT_BLOCKED && (bp->flags & BLOCK_UNBLOCKED))
		bp->result = BLOCK_RESULT_UNBLOCKED;
	if(bp->flags & BLOCK_TIMEOUT)
		timer_remove(&bp->timer);
	bp->bl = NULL;
	return bp->result;
}

__orderedinitializer(__orderedafter(TRACE_INITIALIZER)) static void init_trace(void)
{
	//trace_enable(&blocking_trace);
}

