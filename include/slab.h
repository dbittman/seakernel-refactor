#ifndef __SLAB_H
#define __SLAB_H

#include <stddef.h>
#include <stdbool.h>
#include <lib/stack.h>
#include <lib/linkedlist.h>
#include <spinlock.h>
#include <lib/hash.h>
#include <blocklist.h>

struct cache {
	struct linkedlist empty, partial, full;
	size_t total_slabs;
	size_t total_inuse;
	size_t total_cached;
	struct kobj *kobj;
};

struct kobj {
	const char *name;
	size_t size;
	void (*create)(void *);
	void (*init)(void *);
	void (*put)(void *);
	void (*destroy)(void *);
	bool initialized;
	struct cache cache;
	struct spinlock lock;
};

#define KOBJ_DEFAULT(_name) {\
	.initialized = false, \
	.name = "_name", \
	.size = sizeof(struct _name), \
	.create = NULL, \
	.init = NULL, \
	.destroy = NULL, \
	.put = NULL,}

#define KOBJ_LRU_INIT 1
#define KOBJ_LRU_ERR  2

struct kobj_header {
	_Atomic size_t _koh_refs;
	struct kobj *_koh_kobj;
	struct stack_elem _koh_elem;
	struct slab *_koh_slab;
	bool _koh_initialized;
	struct hashelem idelem;
	struct linkedentry lruentry;
	void *id;
	_Atomic int flags;
};

void *kobj_allocate(struct kobj *ko);
void *kobj_getref(void *obj);
size_t kobj_putref(void *obj);

struct kobj_idmap {
	size_t idlen;
	struct hash hash;
	struct spinlock lock;
};

struct kobj_lru {
	struct kobj *kobj;
	size_t idlen, max;
	struct linkedlist lru, active;
	struct hash hash;
	struct spinlock lock;
	struct blocklist wait;
	void *data;
	bool (*init)(void *, void *, void *);
};

static inline void kobj_idmap_create(struct kobj_idmap *idm, size_t idlen)
{
	hash_create(&idm->hash, HASH_LOCKLESS, 128 /* TODO (minor) [dbittman]: make an intelligent decision about this number */);
	spinlock_create(&idm->lock);
	idm->idlen = idlen;
}

static inline void kobj_idmap_insert(struct kobj_idmap *idm, void *obj, void *id)
{
	struct kobj_header *h = obj;
	spinlock_acquire(&idm->lock);
	kobj_getref(obj);
	hash_insert(&idm->hash, id, idm->idlen, &h->idelem, obj);
	spinlock_release(&idm->lock);
}

static inline void kobj_idmap_delete(struct kobj_idmap *idm, void *obj, void *id)
{
	spinlock_acquire(&idm->lock);
	hash_delete(&idm->hash, id, idm->idlen);
	kobj_putref(obj);
	spinlock_release(&idm->lock);
}

static inline void *kobj_idmap_lookup(struct kobj_idmap *idm, void *id)
{
	spinlock_acquire(&idm->lock);
	void *ret = hash_lookup(&idm->hash, id, idm->idlen);
	kobj_getref(ret);
	spinlock_release(&idm->lock);
	return ret;
}

void kobj_lru_create(struct kobj_lru *lru, size_t idlen, size_t max, struct kobj *kobj,
		bool (*init)(void *obj, void *id, void *data), void *data);
void kobj_lru_mark_ready(struct kobj_lru *lru, void *obj, void *id);
void kobj_lru_mark_error(struct kobj_lru *lru, void *obj, void *id);
void kobj_lru_reclaim(struct kobj_lru *lru);
void *kobj_lru_get(struct kobj_lru *lru, void *id);
void kobj_lru_put(struct kobj_lru *lru, void *obj);
#endif

