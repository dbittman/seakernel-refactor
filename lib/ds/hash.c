#include <stdint.h>
#include <stdbool.h>
#include <lib/linkedlist.h>
#include <mutex.h>
#include <string.h>
#include <lib/hash.h>
#include <mmu.h>
#define __lock(h) do { if(!(h->flags & HASH_LOCKLESS)) spinlock_acquire(&h->lock); } while(0)
#define __unlock(h) do { if(!(h->flags & HASH_LOCKLESS)) spinlock_release(&h->lock); } while(0)

void hash_create(struct hash *h, int flags, size_t length)
{
	spinlock_create(&h->lock);
	h->flags = flags;
	h->table = (void *)mm_virtual_allocate(((length * sizeof(struct linkedlist) - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0), false);
	for(size_t i=0;i<length;i++) {
		linkedlist_create(&h->table[i], (flags & HASH_LOCKLESS) ? LINKEDLIST_LOCKLESS : 0);
	}
	h->length = length;
}

void hash_destroy(struct hash *h)
{
	mm_virtual_deallocate((uintptr_t)h->table);
}

static size_t __hashfn(const void *key, size_t keylen, size_t table_len)
{
	size_t hash = 5381;
	const unsigned char *buf = key;
	for(unsigned int i = 0;i < keylen;i++) {
		unsigned char e = buf[i];
		hash = ((hash << 5) + hash) + e;
	}
	return hash % table_len;
}

static bool __same_keys(const void *key1, size_t key1len, const void *key2, size_t key2len)
{
	if(key1len != key2len)
		return false;
	return memcmp(key1, key2, key1len) == 0 ? true : false;
}

static bool __ll_check_exist(struct linkedentry *ent, void *data)
{
	struct hashelem *he = data;
	struct hashelem *this = ent->obj;
	return __same_keys(he->key, he->keylen, this->key, this->keylen);
}

int hash_insert(struct hash *h, const void *key, size_t keylen, struct hashelem *elem, void *data)
{
	__lock(h);
	size_t index = __hashfn(key, keylen, h->length);
	elem->ptr = data;
	elem->key = key;
	elem->keylen = keylen;
	struct linkedentry *ent = linkedlist_find(&h->table[index], __ll_check_exist, elem);
	if(ent) {
		__unlock(h);
		return -1;
	}
	linkedlist_insert(&h->table[index], &elem->entry, elem);
	h->count++;
	__unlock(h);
	return 0;
}

int hash_delete(struct hash *h, const void *key, size_t keylen)
{
	__lock(h);
	size_t index = __hashfn(key, keylen, h->length);
	struct hashelem tmp;
	tmp.key = key;
	tmp.keylen = keylen;
	struct linkedentry *ent = linkedlist_find(&h->table[index], __ll_check_exist, &tmp);
	if(ent) {
		linkedlist_remove(&h->table[index], ent);
		h->count--;
	}
	__unlock(h);
	return ent ? 0 : -1;
}

void *hash_lookup(struct hash *h, const void *key, size_t keylen)
{
	__lock(h);
	size_t index = __hashfn(key, keylen, h->length);
	struct hashelem tmp;
	tmp.key = key;
	tmp.keylen = keylen;
	struct linkedentry *ent = linkedlist_find(&h->table[index], __ll_check_exist, &tmp);
	void *ret = NULL;
	if(ent) {
		struct hashelem *elem = ent->obj;
		ret = elem->ptr;
	}
	__unlock(h);
	return ret;
}

