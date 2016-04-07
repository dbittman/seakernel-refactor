#pragma once

#include <lib/linkedlist.h>
#include <spinlock.h>
#include <timer.h>

struct blocklist {
	struct linkedlist waitlist;
	struct spinlock lock;
};

enum block_result {
	BLOCK_RESULT_UNBLOCKED,
	BLOCK_RESULT_TIMEOUT,
	BLOCK_RESULT_INTERRUPTED,
};

#define BLOCK_UNINTERRUPT 1
#define BLOCK_TIMEOUT 2
#define BLOCK_UNBLOCKED 4
struct thread;
struct blockpoint {
	struct thread * _Atomic thread;
	struct blocklist *bl;
	_Atomic int flags;
	long timeout;
	_Atomic enum block_result result;
	struct linkedentry node;
	struct timer timer;
};

void blocklist_create(struct blocklist *bl);
bool blocklist_unblock_one(struct blocklist *bl);
void blockpoint_startblock(struct blocklist *bl, struct blockpoint *bp);
enum block_result blockpoint_cleanup(struct blockpoint *bp);
void blockpoint_unblock(struct blockpoint *bp);
void thread_unblock(struct thread *thread);
void blocklist_unblock_all(struct blocklist *bl);

#include <assert.h>
static inline void blockpoint_create(struct blockpoint *bp, int flags, long timeout)
{
	assert(!(flags & BLOCK_UNBLOCKED));
	bp->flags = flags;
	bp->timeout = timeout;
	bp->result = BLOCK_RESULT_UNBLOCKED;
	bp->thread = NULL;
	bp->bl = NULL;
}

