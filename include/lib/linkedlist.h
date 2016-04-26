#ifndef __LIB_LINKEDLIST_H
#define __LIB_LINKEDLIST_H

#define LINKEDLIST_LOCKLESS 1
struct linkedlist;
struct linkedentry {
	void *obj;
	struct linkedlist *list;
	struct linkedentry *next, *prev;
};

#define linkedentry_obj(entry) ((entry) ? (entry)->obj : NULL)

#include <spinlock.h>
#include <stdbool.h>
#include <stddef.h>

struct linkedlist {
	struct linkedentry *head;
	struct linkedentry sentry;
	struct spinlock lock;
	_Atomic ssize_t count;
	int flags;
};

#define linkedlist_iter_end(list) &(list)->sentry
#define linkedlist_iter_start(list) (list)->head->next
#define linkedlist_iter_next(entry) (entry)->next

#define linkedlist_back_iter_end(list) &(list)->sentry
#define linkedlist_back_iter_start(list) (list)->head->prev
#define linkedlist_back_iter_next(entry) (entry)->prev

void __linkedlist_lock(struct linkedlist *list);
void __linkedlist_unlock(struct linkedlist *list);
void *linkedlist_head(struct linkedlist *list);
void *linkedlist_remove_head(struct linkedlist *list);
void *linkedlist_remove_tail(struct linkedlist *list);
void linkedlist_create(struct linkedlist *list, int flags);
void linkedlist_insert(struct linkedlist *list, struct linkedentry *entry, void *obj);
void linkedlist_remove(struct linkedlist *list, struct linkedentry *entry);
struct linkedentry *linkedlist_find(struct linkedlist *list, bool (*fn)(struct linkedentry *, void *data), void *data);

#endif

