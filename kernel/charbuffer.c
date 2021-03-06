#include <charbuffer.h>
#include <thread.h>
#include <assert.h>
#include <string.h>
#include <printk.h>
#include <errno.h>
void charbuffer_create(struct charbuffer *cb, size_t cap)
{
	assert(cap == 0x1000); /* NOTE: for now, lets fix this */
	cb->capacity = cap;
	cb->head = cb->tail = 0;
	cb->pending = 0;
	cb->eof = 0;
	cb->term = false;
	blocklist_create(&cb->wait_write);
	blocklist_create(&cb->wait_read);
	spinlock_create(&cb->read);
	spinlock_create(&cb->write);
	cb->buffer = (void *)mm_virtual_allocate(cap, false);
}

void charbuffer_destroy(struct charbuffer *cb)
{
	mm_virtual_deallocate((uintptr_t)cb->buffer);
}

ssize_t charbuffer_write(struct charbuffer *cb, const char *buf, size_t len, int flags)
{
	spinlock_acquire(&cb->write);
	if(cb->term) {
		spinlock_release(&cb->write);
		return 0;
	}
	size_t amount_written = 0;
	size_t remaining = len;
	while(amount_written != len 
			&& (!(flags & CHARBUFFER_DO_ANY) || amount_written == 0)) {
		size_t head = atomic_load(&cb->head);
		size_t avail = cb->capacity - cb->pending;
		size_t amount = avail > remaining ? remaining : avail;

		head %= cb->capacity;

		size_t thiswrite = cb->capacity - head;
		if(amount > thiswrite)
			amount = thiswrite;
		memcpy(cb->buffer + head, buf + amount_written, amount);

		amount_written += amount;
		remaining -= amount;

		if(amount_written != len 
				&& (!(flags & CHARBUFFER_DO_ANY) || amount_written == 0)) {
			if(flags & CHARBUFFER_DO_NONBLOCK) {
				atomic_fetch_add(&cb->pending, amount);
				atomic_fetch_add(&cb->head, amount);
				spinlock_release(&cb->write);
				return amount_written;
			}
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&cb->wait_write, &bp);
			
			atomic_fetch_add(&cb->pending, amount);
			atomic_fetch_add(&cb->head, amount);
			spinlock_release(&cb->write);
			blocklist_unblock_all(&cb->wait_read);

			int rem = cb->capacity - cb->pending;
			if(rem == 0)
				schedule();
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return amount_written == 0 ? -EINTR : (ssize_t)amount_written;
			}
			spinlock_acquire(&cb->write);
			if(cb->term) {
				spinlock_release(&cb->write);
				return amount_written;
			}
		} else {
			atomic_fetch_add(&cb->pending, amount);
			atomic_fetch_add(&cb->head, amount);
			/* TODO: do we need to release these locks here? */
			spinlock_release(&cb->write);
			blocklist_unblock_all(&cb->wait_read);
			spinlock_acquire(&cb->write);
		}
	}
	spinlock_release(&cb->write);
	blocklist_unblock_all(&cb->wait_read);
	return amount_written;
}

ssize_t charbuffer_read(struct charbuffer *cb, char *buf, size_t len, int flags)
{
	spinlock_acquire(&cb->read);
	if(cb->term && charbuffer_pending(cb) == 0) {
		spinlock_release(&cb->read);
		return 0;
	}
	size_t amount_read = 0;
	size_t remaining = len;
	while(amount_read != len 
			&& (!(flags & CHARBUFFER_DO_ANY) || amount_read == 0)) {
		size_t tail = atomic_load(&cb->tail);
		size_t avail = cb->pending;
		size_t amount = avail > remaining ? remaining : avail;

		tail %= cb->capacity;

		size_t thisread = cb->capacity - tail;
		if(amount > thisread)
			amount = thisread;
		memcpy(buf + amount_read, cb->buffer + tail, amount);
		
		remaining -= amount;
		amount_read += amount;

		if(amount_read != len
				&& (!(flags & CHARBUFFER_DO_ANY) || amount_read == 0)) {
			if(flags & CHARBUFFER_DO_NONBLOCK) {
				atomic_fetch_sub(&cb->pending, amount);
				atomic_fetch_add(&cb->tail, amount);
				spinlock_release(&cb->read);
				return amount_read > 0 ? (ssize_t)amount_read : -EAGAIN;
			}
			if(cb->eof && amount_read == 0) {
				cb->eof = 0;
				spinlock_release(&cb->read);
				return 0;
			}
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&cb->wait_read, &bp);
			
			atomic_fetch_sub(&cb->pending, amount);
			atomic_fetch_add(&cb->tail, amount);
			spinlock_release(&cb->read);

			blocklist_unblock_all(&cb->wait_write);

			int rem = cb->pending;
			if(rem == 0 && !cb->term)
				schedule();
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return amount_read == 0 ? -EINTR : (ssize_t)amount_read;
			}
			spinlock_acquire(&cb->read);
			if(cb->term && charbuffer_pending(cb) == 0) {
				spinlock_release(&cb->read);
				return amount_read;
			}
		} else {
			atomic_fetch_sub(&cb->pending, amount);
			atomic_fetch_add(&cb->tail, amount);
			spinlock_release(&cb->read);
			blocklist_unblock_all(&cb->wait_write);
			spinlock_acquire(&cb->read);
		}
	}
	spinlock_release(&cb->read);
	blocklist_unblock_all(&cb->wait_write);
	return amount_read;
}

void charbuffer_terminate(struct charbuffer *cb)
{
	spinlock_acquire(&cb->read);
	spinlock_acquire(&cb->write);

	cb->term = true;
	blocklist_unblock_all(&cb->wait_write);
	blocklist_unblock_all(&cb->wait_read);

	spinlock_release(&cb->write);
	spinlock_release(&cb->read);
}

void charbuffer_reset(struct charbuffer *cb)
{
	spinlock_acquire(&cb->read);
	spinlock_acquire(&cb->write);

	cb->term = false;
	cb->head = cb->tail = 0;
	cb->pending = 0;
	cb->eof = 0;
	blocklist_unblock_all(&cb->wait_write);
	blocklist_unblock_all(&cb->wait_read);

	spinlock_release(&cb->write);
	spinlock_release(&cb->read);
}

