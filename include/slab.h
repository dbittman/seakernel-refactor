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

#define KOBJ_DEFAULT_ELEM(_name) \
	.initialized = false, \
	.name = #_name, \
	.size = sizeof(struct _name)



#define KOBJ_DEFAULT(_name) {\
	.initialized = false, \
	.name = #_name, \
	.size = sizeof(struct _name), \
	.create = NULL, \
	.init = NULL, \
	.destroy = NULL, \
	.put = NULL,}

#define KOBJ_LRU_INIT 1
#define KOBJ_LRU_ERR  2
#define KOBJ_LRU 1024

#define KOBJ_HEADER_MAGIC 0x66883322CAFEBEEFull
struct kobj_header {
	_Atomic ssize_t _koh_refs;
	_Atomic uint64_t magic;
	struct kobj *_koh_kobj;
	struct stack_elem _koh_elem;
	struct slab *_koh_slab;
	bool _koh_initialized;
	struct hashelem idelem;
	struct linkedentry lruentry;
	_Atomic int flags;
	struct blocklist wait;
	void * _Atomic id;
};

void *kobj_allocate(struct kobj *ko);
void *kobj_getref(void *obj);
size_t __kobj_putref(void *obj);

static inline size_t kobj_putref(void *obj)
{
	struct kobj_header *header = obj;
	assert(header->magic == KOBJ_HEADER_MAGIC);
	if(header->flags)
		panic(0, "called normal putref with flags non-zero %p: %s %x (%d)\n", obj, header->_koh_kobj->name, header->flags, *(int *)header->id);
	assert(header->_koh_initialized);
	return __kobj_putref(obj);
}

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
	void *data;
	bool (*init)(void *, void *, void *);
	void (*release)(void *, void *);
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
	if(hash_insert(&idm->hash, id, idm->idlen, &h->idelem, obj) == 0)
		kobj_getref(obj);
	spinlock_release(&idm->lock);
}

static inline void kobj_idmap_delete(struct kobj_idmap *idm, void *obj, void *id)
{
	spinlock_acquire(&idm->lock);
	if(hash_delete(&idm->hash, id, idm->idlen) == 0)
		kobj_putref(obj);
	spinlock_release(&idm->lock);
}

static inline void *kobj_idmap_lookup(struct kobj_idmap *idm, void *id)
{
	spinlock_acquire(&idm->lock);
	void *ret = hash_lookup(&idm->hash, id, idm->idlen);
	if(ret) kobj_getref(ret);
	spinlock_release(&idm->lock);
	return ret;
}

static inline void kobj_idmap_lock(struct kobj_idmap *idm)
{
	spinlock_acquire(&idm->lock);
}

static inline void kobj_idmap_unlock(struct kobj_idmap *idm)
{
	spinlock_release(&idm->lock);
}

static inline void kobj_idmap_iter_init(struct kobj_idmap *idm, struct hashiter *iter)
{
	hash_iter_init(iter, &idm->hash);
}

static inline bool kobj_idmap_iter_done(struct hashiter *iter)
{
	return hash_iter_done(iter);
}

static inline void kobj_idmap_iter_next(struct hashiter *iter)
{
	hash_iter_next(iter);
}

static inline void *kobj_idmap_iter_get(struct hashiter *iter)
{
	return hash_iter_get(iter);
}

void kobj_lru_create(struct kobj_lru *lru, size_t idlen, size_t max, struct kobj *kobj,
		bool (*init)(void *obj, void *id, void *data), void (*release)(void *, void *), void *data);
void kobj_lru_mark_ready(struct kobj_lru *lru, void *obj, void *id);
void kobj_lru_mark_error(struct kobj_lru *lru, void *obj, void *id);
void kobj_lru_reclaim(struct kobj_lru *lru);
void *kobj_lru_get(struct kobj_lru *lru, void *id);
void kobj_lru_put(struct kobj_lru *lru, void *obj);
void kobj_lru_release_all(struct kobj_lru *lru);
void kobj_lru_destroy(struct kobj_lru *lru);
#endif

