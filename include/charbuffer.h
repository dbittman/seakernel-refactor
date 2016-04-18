#pragma once

#include <stddef.h>
#include <blocklist.h>

#define CHARBUFFER_DO_ANY      1
#define CHARBUFFER_DO_NONBLOCK 2

struct charbuffer {
	_Atomic char *buffer;
	size_t capacity;
	_Atomic size_t head, tail;
	struct blocklist wait_write, wait_read;
	struct spinlock write, read;
	_Atomic bool term;
	int eof;
};

static inline size_t charbuffer_pending(struct charbuffer *cb) { return cb->head - cb->tail; }
static inline size_t charbuffer_avail(struct charbuffer *cb)
{ return cb->capacity - (cb->head - cb->tail); }

void charbuffer_create(struct charbuffer *cb, size_t cap);
size_t charbuffer_write(struct charbuffer *cb, const char *buf, size_t len, int flags);
size_t charbuffer_read(struct charbuffer *cb, char *buf, size_t len, int flags);
void charbuffer_terminate(struct charbuffer *cb);
void charbuffer_destroy(struct charbuffer *cb);
void charbuffer_reset(struct charbuffer *cb);

