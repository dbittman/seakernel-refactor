#include <slab.h>
#include <thread.h>
#include <blocklist.h>
#include <assert.h>
#include <printk.h>
void kobj_lru_create(struct kobj_lru *lru, size_t idlen, size_t max, struct kobj *kobj,
		bool (*init)(void *obj, void *id, void *), void *data)
{
	lru->kobj = kobj;
	lru->init = init;
	lru->max = max;
	lru->idlen = idlen;
	lru->data = data;
	hash_create(&lru->hash, HASH_LOCKLESS, 2048 / sizeof(struct linkedlist));
	linkedlist_create(&lru->lru, LINKEDLIST_LOCKLESS);
	linkedlist_create(&lru->active, LINKEDLIST_LOCKLESS);
	blocklist_create(&lru->wait);
	spinlock_create(&lru->lock);
}

void kobj_lru_mark_ready(struct kobj_lru *lru, void *obj, void *id)
{
	struct kobj_header *header = obj;
	spinlock_acquire(&lru->lock);
	/* yes, we need to do this. The ID field must be stored inside the
	 * object, and up to here it is not. */
	header->id = id;
	hash_delete(&lru->hash, id, lru->idlen);
	hash_insert(&lru->hash, header->id, lru->idlen, &header->idelem, obj);
	spinlock_release(&lru->lock);
	header->flags |= KOBJ_LRU_INIT;
	blocklist_unblock_all(&lru->wait);
}

void kobj_lru_mark_error(struct kobj_lru *lru, void *obj, void *id)
{
	struct kobj_header *header = obj;
	spinlock_acquire(&lru->lock);
	
	hash_delete(&lru->hash, id, lru->idlen);
	linkedlist_remove(&lru->active, &header->lruentry);

	header->id = id;
	spinlock_release(&lru->lock);

	header->flags |= KOBJ_LRU_ERR;
	header->flags |= KOBJ_LRU_INIT;
	blocklist_unblock_all(&lru->wait);
	kobj_putref(obj);
	kobj_putref(obj);
}

static void *_do_kobj_lru_reclaim(struct kobj_lru *lru)
{
	void *obj;
	obj = linkedlist_remove_tail(&lru->lru);
	if(obj) {
		struct kobj_header *header = obj;
		assert(header->_koh_refs == 2);
		hash_delete(&lru->hash, header->id, lru->idlen);
		kobj_putref(obj);
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
		kobj_putref(obj);
}

/* gets an object, and if not active, it moves off the LRU */
void *kobj_lru_get(struct kobj_lru *lru, void *id)
{
	spinlock_acquire(&lru->lock);

	void *obj = hash_lookup(&lru->hash, id, lru->idlen);
	if(obj) {
		struct kobj_header *header = obj;
		kobj_getref(obj);
		if(!(header->flags & KOBJ_LRU_INIT)) {
			struct blockpoint bp;
			blockpoint_create(&bp, BLOCK_UNINTERRUPT, 0);
			blockpoint_startblock(&lru->wait, &bp);
			if(header->flags & KOBJ_LRU_INIT) {
				blockpoint_unblock(&bp);
			} else {
				spinlock_release(&lru->lock);
				schedule();
				spinlock_acquire(&lru->lock);
			}
			blockpoint_cleanup(&bp);
		}
		if(header->flags & KOBJ_LRU_ERR) {
			kobj_putref(obj);
			spinlock_release(&lru->lock);
			return NULL;
		}
		/* in hash and lru (and the inc we just did) -> no one else has reference -> in LRU */
		if(header->_koh_refs == 3) {
			linkedlist_remove(&lru->lru, &header->lruentry);
			linkedlist_insert(&lru->active, &header->lruentry, obj);
		}
		spinlock_release(&lru->lock);
	} else {
		if(lru->hash.count >= lru->max && lru->max > 0)
			obj = _do_kobj_lru_reclaim(lru);

		if(obj == NULL)
			obj = kobj_allocate(lru->kobj);
		struct kobj_header *header = obj;
		linkedlist_insert(&lru->active, &header->lruentry, kobj_getref(obj));
		hash_insert(&lru->hash, id, lru->idlen, &header->idelem, kobj_getref(obj));
		header->_koh_refs = 3;
		spinlock_release(&lru->lock);
		if(!lru->init(obj, id, lru->data)) {
			kobj_putref(obj);
			return NULL;
		}
	}
	return obj;
}

void kobj_lru_put(struct kobj_lru *lru, void *obj)
{
	spinlock_acquire(&lru->lock);

	kobj_putref(obj);

	struct kobj_header *header = obj;
	if(header->_koh_refs == 2) {
		printk("LRUPUT move to lru\n");
		linkedlist_remove(&lru->active, &header->lruentry);
		linkedlist_insert(&lru->lru, &header->lruentry, obj);
	}

	spinlock_release(&lru->lock);
}


