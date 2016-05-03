#include <slab.h>
#include <thread.h>
#include <blocklist.h>
#include <assert.h>
#include <printk.h>
#include <errno.h>
#include <fs/proc.h>
void kobj_lru_create(struct kobj_lru *lru, size_t idlen, size_t max, struct kobj *kobj,
		bool (*init)(void *obj, void *id, void *), void (*release)(void *obj, void *), void *data)
{
	lru->kobj = kobj;
	lru->init = init;
	lru->max = max;
	lru->idlen = idlen;
	lru->data = data;
	lru->release = release;
	hash_create(&lru->hash, HASH_LOCKLESS, 2048 / sizeof(struct linkedlist));
	linkedlist_create(&lru->lru, LINKEDLIST_LOCKLESS);
	linkedlist_create(&lru->active, LINKEDLIST_LOCKLESS);
	spinlock_create(&lru->lock);
}

void kobj_lru_destroy(struct kobj_lru *lru)
{
	panic(0, "untested");
	assert(lru->hash.count == 0);
	hash_destroy(&lru->hash);
}

void kobj_lru_release_all(struct kobj_lru *lru)
{
	panic(0, "untested");
	spinlock_acquire(&lru->lock);
	
	struct hashiter iter;
	for(hash_iter_init(&iter, &lru->hash);
			!hash_iter_done(&iter); hash_iter_next(&iter)) {
		void *obj = hash_iter_get(&iter);
		struct kobj_header *header = obj;
		assert(header->_koh_refs == 2);

		int r = hash_delete(&lru->hash, header->id, lru->idlen);
		assert(r == 0);

		linkedlist_remove(&lru->lru, &header->lruentry);

		lru->release(obj, lru->data);

		__kobj_putref(obj);
		__kobj_putref(obj);
	}

	spinlock_release(&lru->lock);
}

ssize_t kobj_lru_proc_read(void *data, int rw, size_t off, size_t len, char *buf)
{
	size_t current = 0;
	if(rw != 0)
		return -EINVAL;
	struct kobj_lru_proc_info *opt = data;

	spinlock_acquire(&opt->lru->lock);
	
	struct hashiter iter;
	for(hash_iter_init(&iter, &opt->lru->hash);
			!hash_iter_done(&iter); hash_iter_next(&iter)) {
		void *obj = hash_iter_get(&iter);
		struct kobj_header *header = obj;

		if(header->_koh_refs == 2) {
			PROCFS_PRINTF(off, len, buf, current,
					" LRU: ");
		} else {
			PROCFS_PRINTF(off, len, buf, current,
					"USED, %ld refs: ", header->_koh_refs - 2);
		}

		if((ssize_t)len - (ssize_t)current > 0)
			current += opt->read_entry(obj, off, len - current, buf + current);

		PROCFS_PRINTF(off, len, buf, current,
				"\n");
	}

	spinlock_release(&opt->lru->lock);
	return current;
}

void kobj_lru_mark_ready(struct kobj_lru *lru, void *obj, void *id)
{
	struct kobj_header *header = obj;
	spinlock_acquire(&lru->lock);
	/* yes, we need to do this. The ID field must be stored inside the
	 * object, and up to here it is not. */
	header->id = id;
	hash_delete(&lru->hash, id, lru->idlen);
	hash_insert(&lru->hash, id, lru->idlen, &header->idelem, obj);
	header->flags |= KOBJ_LRU_INIT;
	spinlock_release(&lru->lock);
	blocklist_unblock_all(&header->wait);
}

void kobj_lru_mark_error(struct kobj_lru *lru, void *obj, void *id)
{
	struct kobj_header *header = obj;
	spinlock_acquire(&lru->lock);
	
	hash_delete(&lru->hash, id, lru->idlen);
	linkedlist_remove(&lru->active, &header->lruentry);

	header->id = id;

	header->flags |= KOBJ_LRU_ERR;
	header->flags |= KOBJ_LRU_INIT;
	spinlock_release(&lru->lock);
	blocklist_unblock_all(&header->wait);
	__kobj_putref(obj);
	__kobj_putref(obj);
}

static void *_do_kobj_lru_reclaim(struct kobj_lru *lru)
{
	panic(0, "untested");
	void *obj;
	obj = linkedlist_remove_tail(&lru->lru);
	if(obj) {
		struct kobj_header *header = obj;
		assert(header->_koh_refs == 2);
		hash_delete(&lru->hash, header->id, lru->idlen);
		__kobj_putref(obj);
	}
	return obj;
}

void kobj_lru_reclaim(struct kobj_lru *lru)
{
	void *obj;
	spinlock_acquire(&lru->lock);
	obj = _do_kobj_lru_reclaim(lru);
	spinlock_release(&lru->lock);
	if(obj)
		__kobj_putref(obj);
}

/* gets an object, and if not active, it moves off the LRU */
void *kobj_lru_get(struct kobj_lru *lru, void *id)
{
	spinlock_acquire(&lru->lock);

	void *obj = hash_lookup(&lru->hash, id, lru->idlen);
	if(obj) {
		struct kobj_header *header = obj;
		assert(header->magic == KOBJ_HEADER_MAGIC);
		assert(header->flags & KOBJ_LRU);
		ssize_t ref = header->_koh_refs++;
	//	kobj_getref(obj);
		if(!(header->flags & KOBJ_LRU_INIT)) {
			struct blockpoint bp;
			blockpoint_create(&bp, BLOCK_UNINTERRUPT, 0);
			blockpoint_startblock(&header->wait, &bp);
			if(!(header->flags & KOBJ_LRU_INIT)) {
				spinlock_release(&lru->lock);
				schedule();
				spinlock_acquire(&lru->lock);
			}
			enum block_result res = blockpoint_cleanup(&bp);
			assert(res == BLOCK_RESULT_UNBLOCKED);
		}
		if(!(header->flags & KOBJ_LRU_INIT)) {
			panic(0, "failed to wait for init state on %p: %x\n", obj, header->flags);
		}
		if(header->flags & KOBJ_LRU_ERR) {
			__kobj_putref(obj);
			spinlock_release(&lru->lock);
			return NULL;
		}
		/* in hash and lru (and the inc we just did) -> no one else has reference -> in LRU */
		assert(header->_koh_refs > 2);
		if(ref == 2) {
			if(lru->lru.count == 0) {
				panic(0, "failed to move object off of LRU: LRU has no entries %d %d %ld %x (%p %p %p)", *(int *)id, *(int *)header->id, header->_koh_refs, header->flags, header->lruentry.list, &lru->lru, &lru->active);
			}
			linkedlist_remove(&lru->lru, &header->lruentry);
			linkedlist_insert(&lru->active, &header->lruentry, obj);
		}
		spinlock_release(&lru->lock);
	} else {
		if(lru->hash.count >= lru->max && lru->max > 0) {
			panic(0, "untested");
			obj = _do_kobj_lru_reclaim(lru);
		}

		if(obj == NULL) {
			obj = kobj_allocate(lru->kobj);
		}
		struct kobj_header *header = obj;
		header->flags = KOBJ_LRU;
		blocklist_create(&header->wait);
		linkedlist_insert(&lru->active, &header->lruentry, kobj_getref(obj));
		hash_insert(&lru->hash, id, lru->idlen, &header->idelem, kobj_getref(obj));
		assert(header->_koh_refs == 3);
		spinlock_release(&lru->lock);
		if(!lru->init(obj, id, lru->data)) {
			__kobj_putref(obj);
			return NULL;
		}
	}
	return obj;
}

void kobj_lru_put(struct kobj_lru *lru, void *obj)
{
	spinlock_acquire(&lru->lock);
	struct kobj_header *header = obj;
	assert(header->magic == KOBJ_HEADER_MAGIC);
	assert(header->flags & KOBJ_LRU);
	int ref = __kobj_putref(obj);
	if(header->_koh_refs < 2 || ref < 2)
		panic(0, "double free");
	if(ref == 2) {
		if(lru->active.count == 0) {
			panic(0, "failed to move object onto LRU: active has no entries %d %ld %x (%p %p %p)", *(int *)header->id, header->_koh_refs, header->flags, header->lruentry.list, &lru->lru, &lru->active);
		}
		linkedlist_remove(&lru->active, &header->lruentry);
		linkedlist_insert(&lru->lru, &header->lruentry, obj);
		spinlock_release(&lru->lock);
		if(header->_koh_kobj->put)
			header->_koh_kobj->put(obj);
	} else {
		spinlock_release(&lru->lock);
	}
}

