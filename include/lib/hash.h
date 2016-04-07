#ifndef __LIB_HASH
#define __LIB_HASH

#include <stdint.h>
#include <spinlock.h>
#include <lib/linkedlist.h>

#define HASH_LOCKLESS 1
struct hashelem {
	void *ptr;
	const void *key;
	size_t keylen;
	struct linkedentry entry;
};

struct hash {
	struct linkedlist *table;
	_Atomic size_t length, count;
	int flags;
	struct spinlock lock;
};

static inline size_t hash_count(struct hash *h) { return h->count; }
static inline size_t hash_length(struct hash *h) { return h->length; }

void hash_create(struct hash *h, int flags, size_t length);
void hash_destroy(struct hash *h);
int hash_insert(struct hash *h, const void *key, size_t keylen, struct hashelem *elem, void *data);
int hash_delete(struct hash *h, const void *key, size_t keylen);
void *hash_lookup(struct hash *h, const void *key, size_t keylen);

#endif
