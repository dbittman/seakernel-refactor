#include <workqueue.h>
#include <lib/linkedlist.h>
#include <stdbool.h>
#include <interrupt.h>

/* NOTE: we need to disable interrupts through
 * these functions since we may want to enqueue
 * work from inside and interrupt handler, which
 * could lead to a deadlock. */

void workqueue_create(struct workqueue *wq)
{
	linkedlist_create(&wq->list, 0);
}

void workqueue_insert(struct workqueue *wq, struct workitem *item)
{
	int old = arch_interrupt_set(0);
	linkedlist_insert(&wq->list, &item->listitem, item);
	arch_interrupt_set(old);
}

bool workqueue_execute(struct workqueue *wq)
{
	int old = arch_interrupt_set(0);
	struct workitem *item = linkedlist_remove_head(&wq->list);
	arch_interrupt_set(old);

	if(item) {
		item->fn(item->arg);
		return true;
	}
	return false;
}

