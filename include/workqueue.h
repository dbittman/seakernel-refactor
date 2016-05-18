#ifndef __WORKQUEUE_H
#define __WORKQUEUE_H

#include <lib/linkedlist.h>
#include <stdbool.h>

struct workitem {
	void (*fn)(void *);
	void *arg;
	struct linkedentry listitem;
};

struct workqueue {
	struct linkedlist list;
};

static inline bool workqueue_empty(struct workqueue *wq)
{
	return wq->list.count == 0;
}

void workqueue_create(struct workqueue *wq);
void workqueue_insert(struct workqueue *wq, struct workitem *item);
bool workqueue_execute(struct workqueue *wq);

#endif

