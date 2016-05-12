#include <lib/linkedlist.h>
#include <stdbool.h>
#include <spinlock.h>
#include <assert.h>
#include <system.h>

inline void __linkedlist_lock(struct linkedlist *list)
{
	if(likely(!(list->flags & LINKEDLIST_LOCKLESS))) {
		spinlock_acquire(&list->lock);
	}
}

inline void __linkedlist_unlock(struct linkedlist *list)
{
	if(likely(!(list->flags & LINKEDLIST_LOCKLESS))) {
		spinlock_release(&list->lock);
	}
}

static void linkedlist_do_remove(struct linkedlist *list, struct linkedentry *entry)
{
	assert(entry != &list->sentry);
	assert(entry->list == list);
	assert(list->count > 0);
	list->count--;
	entry->list = NULL;
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
}

void *linkedlist_head(struct linkedlist *list)
{
	void *ret = NULL;
	__linkedlist_lock(list);
	if(list->head->next != &list->sentry)
		ret = list->head->next->obj;
	__linkedlist_unlock(list);
	return ret;
}

void *linkedlist_remove_head(struct linkedlist *list)
{
	void *ret = NULL;
	__linkedlist_lock(list);
	if(list->head->next != &list->sentry) {
		ret = list->head->next->obj;
		linkedlist_do_remove(list, list->head->next);
	}
	__linkedlist_unlock(list);
	return ret;
}

void *__linkedlist_remove_tail(struct linkedlist *list, bool locked)
{
	void *ret = NULL;
	if(!locked)
		__linkedlist_lock(list);
	if(list->head->prev != &list->sentry) {
		ret = list->head->prev->obj;
		linkedlist_do_remove(list, list->head->prev);
	}
	if(!locked)
		__linkedlist_unlock(list);
	return ret;
}

void linkedlist_create(struct linkedlist *list, int flags)
{
	if(!(flags & LINKEDLIST_LOCKLESS)) {
		spinlock_create(&list->lock);
	}
	list->flags = flags;
	list->head = &list->sentry;
	list->head->next = list->head;
	list->head->prev = list->head;
	list->count = 0;
}

void linkedlist_insert(struct linkedlist *list, struct linkedentry *entry, void *obj)
{
	assert(list->head == &list->sentry);
	assert(list->head->next && list->head->prev);
	assert(list->count >= 0);
	__linkedlist_lock(list);
	entry->next = list->head->next;
	entry->prev = list->head;
	entry->prev->next = entry;
	entry->next->prev = entry;
	entry->obj = obj;
	entry->list = list;
	list->count++;
	assert(list->count > 0);
	__linkedlist_unlock(list);
}

void linkedlist_remove(struct linkedlist *list, struct linkedentry *entry)
{
	assert(list->head == &list->sentry);
	__linkedlist_lock(list);
	linkedlist_do_remove(list, entry);
	__linkedlist_unlock(list);
}

struct linkedentry *linkedlist_find(struct linkedlist *list, bool (*fn)(struct linkedentry *, void *data), void *data)
{
        __linkedlist_lock(list);
        struct linkedentry *ent = list->head->next;
        while(ent != &list->sentry) {
                if(fn(ent, data))
                        break;
                ent = ent->next;
        }
        __linkedlist_unlock(list);
        if(ent == &list->sentry)
                return NULL;
        return ent;
}

