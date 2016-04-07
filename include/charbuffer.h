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
};

void charbuffer_create(struct charbuffer *cb, size_t cap);
size_t charbuffer_write(struct charbuffer *cb, char *buf, size_t len, int flags);
size_t charbuffer_read(struct charbuffer *cb, char *buf, size_t len, int flags);

