#include <slab.h>
#include <stddef.h>
#include <printk.h>
#include <lib/stack.h>
#include <lib/linkedlist.h>
#include <stdatomic.h>
#include <assert.h>
#include <mmu.h>
#include <trace.h>
#include <fs/proc.h>
#include <errno.h>
#include <system.h>
/* For now, this is reasonable. Objects
 * that we're going to allocate here should be
 * small. */
#define SLAB_SIZE arch_mm_page_size(0) * 32

/* TODO (minor) [dbittman]: need a way to
 * reclaim slabs. When we do that, we need
 * to call the destruction callback :) */

struct slab {
	size_t max;
	_Atomic size_t count;
	struct linkedentry entry;
	struct stack objects;
};

TRACE_DEFINE(kobj_trace, "kobj");

static bool cache_list_init = false;
static struct linkedlist cache_list;

ssize_t _slab_proc_read(void *data, int rw, size_t off, size_t len, char *buf)
{
	(void)data;
	size_t current = 0;
	if(rw != 0)
		return -EINVAL;
	PROCFS_PRINTF(off, len, buf, current,
			"                NAME    INUSE    TOTAL\n");
	__linkedlist_lock(&cache_list);
	struct linkedentry *entry;
	for(entry = linkedlist_iter_start(&cache_list);
			entry != linkedlist_iter_end(&cache_list);
			entry = linkedlist_iter_next(entry)) {
		struct cache *cache = linkedentry_obj(entry);

		PROCFS_PRINTF(off, len, buf, current,
				"%20s %8d %8d\n", cache->kobj->name, cache->total_inuse, cache->total_slabs);
	}
	__linkedlist_unlock(&cache_list);
	return current;
}

static void _late_init(void)
{
	proc_create("/proc/slab", _slab_proc_read, NULL);
}
LATE_INIT_CALL(_late_init, NULL);

static void initialize_cache(struct kobj *ko)
{
	struct cache *cache = &ko->cache;
	if(!cache_list_init) {
		cache_list_init = true;
		linkedlist_create(&cache_list, 0);
	}
	cache->kobj = ko;
	/* we don't need locks in the linked lists because the
	 * cache itself is locked. */
	linkedlist_create(&cache->empty, LINKEDLIST_LOCKLESS);
	linkedlist_create(&cache->partial, LINKEDLIST_LOCKLESS);
	linkedlist_create(&cache->full, LINKEDLIST_LOCKLESS);
	cache->total_slabs = cache->total_inuse = 0;

	linkedlist_insert(&cache_list, &cache->listelem, cache);
}

static struct slab *create_new_slab(struct cache *cache)
{
	void *start = (void *)mm_virtual_allocate(SLAB_SIZE, true);
	struct slab *slab = start;
	size_t total_size = cache->kobj->size;
	slab->count = 0;
	/* align start of object array to 16 bytes. */
	size_t slabstruct_size = sizeof(struct slab);
	slabstruct_size = ((slabstruct_size - 1) & ~0xF) + 16;
	slab->max = (SLAB_SIZE - slabstruct_size) / total_size - 1;
	/* in order to avoid lockiness in slabs, we use a locked stack */
	stack_create(&slab->objects, 0);
	/* push all the objects. This is a one-time upfront cost, which
	 * is admittidly somewhat expensive. But it makes actual allocations
	 * simpler and quick. In theory. */
	for(size_t i=0;i<slab->max;i++) {
		struct kobj_header *obj = (void *)((uintptr_t)start
				+ slabstruct_size + i * total_size);
		/* this is a useful pointer to have */
		obj->_koh_kobj = cache->kobj;
		obj->_koh_slab = slab;
		obj->magic = KOBJ_HEADER_MAGIC;
		stack_push(&slab->objects, &obj->_koh_elem, obj);
	}
	return slab;
}

static void *allocate_from_cache(struct cache *cache)
{
	struct slab *slab;
	bool new = false;
	/* step one: look through the lists to find an available
	 * spot in a slab. Try partially filled slabs first to
	 * try to be cache friendly. */
	spinlock_acquire(&cache->kobj->lock);
	if(!(slab = linkedlist_head(&cache->partial))) {
		if(!(slab = linkedlist_head(&cache->empty))) {
			/* didn't find one, so allocate a new one */
			slab = create_new_slab(cache);
			cache->total_slabs++;
			new = true;
		}
	}
	size_t count = atomic_fetch_add(&slab->count, 1);
	/* in some cases, we need to move the slab to a new list. */
	if(count == slab->max - 1) {
		linkedlist_remove(&cache->partial, &slab->entry);
		linkedlist_insert(&cache->full, &slab->entry, slab);
	} else if(count == 0) {
		/* special case: don't remove the slab from the empty list
		 * if it's new, since it won't be there. */
		if(!new)
			linkedlist_remove(&cache->empty, &slab->entry);
		linkedlist_insert(&cache->partial, &slab->entry, slab);
		cache->total_inuse++;
	}
	spinlock_release(&cache->kobj->lock);
	/* now that we have a slab that we want to use, pop and object
	 * from the stack and return it */
	void *obj = stack_pop(&slab->objects);
	assert(obj != NULL);
	return obj;
}

static void deallocate_object(void *obj)
{
	struct kobj_header *header = obj;
	struct slab *slab = header->_koh_slab;
	stack_push(&slab->objects, &header->_koh_elem, obj);
	spinlock_acquire(&header->_koh_kobj->lock);
	size_t count = atomic_fetch_sub(&slab->count, 1);
	/* just like in allocation, we need to mobe the slabs around sometimes */
	if(count == slab->max) {
		linkedlist_remove(&header->_koh_kobj->cache.full, &slab->entry);
		linkedlist_insert(&header->_koh_kobj->cache.partial, &slab->entry, slab);
	} else if(count == 1) {
		linkedlist_remove(&header->_koh_kobj->cache.partial, &slab->entry);
		linkedlist_insert(&header->_koh_kobj->cache.empty, &slab->entry, slab);
		header->_koh_kobj->cache.total_inuse--;
	}
	spinlock_release(&header->_koh_kobj->lock);
}

void *kobj_allocate(struct kobj *ko)
{
	spinlock_acquire(&ko->lock);
	if(!ko->initialized) {
		initialize_cache(ko);
		ko->initialized = true;
	}
	spinlock_release(&ko->lock);
	void *obj = allocate_from_cache(&ko->cache);
	struct kobj_header *header = obj;
	header->_koh_refs = 1;
	header->flags = 0;
	assert(header->magic == KOBJ_HEADER_MAGIC);
	/* so, here we maintain if this object has
	 * been allocated before. If it has, we don't need
	 * to create it. Instead, we just need to re-initialize
	 * it. */
	if(header->_koh_initialized == false) {
		header->_koh_initialized = true;
		TRACE(&kobj_trace, "create object: %s - %p\n", ko->name, obj);
		if(ko->create)
			ko->create(obj);
	} else {
		TRACE(&kobj_trace, "init object: %s - %p\n", ko->name, obj);
		if(ko->init)
			ko->init(obj);
	}
	return obj;
}

size_t __kobj_putref(void *obj)
{
	assert(obj != NULL);
	struct kobj_header *header = obj;
	assert(header->magic == KOBJ_HEADER_MAGIC);
	ssize_t count = atomic_fetch_sub(&header->_koh_refs, 1);
#if CONFIG_DEBUG
	if(count <= 0) {
		panic(0, "double-free of object %p, %s", obj, header->_koh_kobj->name);
	}
#endif
	assert(count > 0);
	if(count == 1) {
		TRACE(&kobj_trace, "put object: %s - %p\n", header->_koh_kobj->name, obj);
		if(header->_koh_kobj->put)
			header->_koh_kobj->put(obj);
		deallocate_object(obj);
	}
	return count - 1;
}


