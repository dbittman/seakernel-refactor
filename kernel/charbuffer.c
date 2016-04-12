#include <charbuffer.h>
#include <thread.h>
#include <assert.h>
#include <string.h>
#include <printk.h>
void charbuffer_create(struct charbuffer *cb, size_t cap)
{
	assert(cap == 0x1000); /* NOTE: for now, lets fix this */
	cb->capacity = cap;
	cb->head = cb->tail = 0;
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

size_t charbuffer_write(struct charbuffer *cb, const char *buf, size_t len, int flags)
{
	spinlock_acquire(&cb->write);
	size_t amount_written = 0;
	size_t remaining = len;
	while(amount_written != len 
			&& (!(flags & CHARBUFFER_DO_ANY) || amount_written == 0)) {
		size_t head = atomic_load(&cb->head);
		size_t tail = atomic_load(&cb->tail);
		size_t avail = cb->capacity - (head - tail);
		size_t amount = avail > remaining ? remaining : avail;

		head %= cb->capacity;
		tail %= cb->capacity;
		/* break into two parts, since we might wrap around */
		size_t part1_amount = cb->capacity - head;
		if(part1_amount > amount)
			part1_amount = amount;
		if(part1_amount)
			memcpy(cb->buffer + head, buf, part1_amount);
		size_t part2_amount = amount - part1_amount;
		/* note that if we have any left to write, we'll be writing at the start */
		if(part2_amount)
			memcpy(cb->buffer, buf + part1_amount, part2_amount);

		amount_written += amount;
		remaining -= amount;

		if(amount_written != len 
				&& (!(flags & CHARBUFFER_DO_ANY) || amount_written == 0)) {
			if(flags & CHARBUFFER_DO_NONBLOCK) {
				spinlock_release(&cb->write);
				return amount_written;
			}
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&cb->wait_write, &bp);
			
			atomic_fetch_add(&cb->head, amount);
			spinlock_release(&cb->write);
			/* TODO: handle signals here */
			blocklist_unblock_one(&cb->wait_read);

			int rem = cb->capacity - (atomic_load(&cb->head) - atomic_load(&cb->tail));
			if(rem == 0)
				schedule();
			else
				blockpoint_unblock(&bp);
			blockpoint_cleanup(&bp);
			spinlock_acquire(&cb->write);
			buf += amount;
		} else {
			atomic_fetch_add(&cb->head, amount);
			blocklist_unblock_one(&cb->wait_read);
		}
	}
	spinlock_release(&cb->write);
	return amount_written;
}

size_t charbuffer_read(struct charbuffer *cb, char *buf, size_t len, int flags)
{
	spinlock_acquire(&cb->read);
	size_t amount_read = 0;
	size_t remaining = len;
	while(amount_read != len 
			&& (!(flags & CHARBUFFER_DO_ANY) || amount_read == 0)) {
		size_t head = atomic_load(&cb->head);
		size_t tail = atomic_load(&cb->tail);
		size_t avail = (head - tail);
		size_t amount = avail > remaining ? remaining : avail;

		head %= cb->capacity;
		tail %= cb->capacity;

		/* break into two parts, since we might wrap around */
		size_t part1_amount = cb->capacity - tail;
		if(part1_amount > amount)
			part1_amount = amount;
		if(part1_amount)
			memcpy(buf, cb->buffer + tail, part1_amount);
		size_t part2_amount = amount - part1_amount;
		/* note that if we have any left to write, we'll be writing at the start */
		if(part2_amount)
			memcpy(buf + part1_amount, cb->buffer, part2_amount);

		remaining -= amount;
		amount_read += amount;

		if(amount_read != len
				&& (!(flags & CHARBUFFER_DO_ANY) || amount_read == 0)) {
			if(flags & CHARBUFFER_DO_NONBLOCK) {
				spinlock_release(&cb->read);
				return amount_read;
			}
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&cb->wait_read, &bp);
			
			atomic_fetch_add(&cb->tail, amount);
			spinlock_release(&cb->read);

			blocklist_unblock_one(&cb->wait_write);

			int rem = (atomic_load(&cb->head) - atomic_load(&cb->tail));
			if(rem == 0)
				schedule();
			else
				blockpoint_unblock(&bp);
			blockpoint_cleanup(&bp);
			spinlock_acquire(&cb->read);
			buf += amount;
		} else {
			atomic_fetch_add(&cb->tail, amount);
			blocklist_unblock_one(&cb->wait_write);
		}
	}
	spinlock_release(&cb->read);
	return amount_read;
}

