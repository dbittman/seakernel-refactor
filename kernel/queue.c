#include <priqueue.h>
#include <printk.h>
#include <assert.h>
#include <processor.h>
#define DEBUG_PRIQUEUE 0

void priqueue_create(struct priqueue *pq, int levels)
{
	for(int i=0;i<PRIQUEUE_LEVELS;i++)
		linkedlist_create(&pq->lists[i], LINKEDLIST_LOCKLESS);
	pq->levels = levels;
	pq->curhighest = 0;
	spinlock_create(&pq->lock);
}

void priqueue_insert(struct priqueue *pq, struct priqueue_node *node, void *data, int pri)
{
	spinlock_acquire(&pq->lock);
	/* we only scale the incoming priority levels to the number we can handle if we need
	 * to. If we have unused queues, that's okay. */
	int q = pq->levels > PRIQUEUE_LEVELS ? (pri * PRIQUEUE_LEVELS) / pq->levels : pri;
	if(q < 0) q = 0;
	if(q >= pq->levels) q = pq->levels-1;

#if DEBUG_PRIQUEUE || 1
	printk("%d Insert %d\n", current_thread->processor->id, q);
#endif
	linkedlist_insert(&pq->lists[q], &node->entry, data);
	if(q > pq->curhighest)
		pq->curhighest = q;
#if DEBUG_PRIQUEUE
	for(int i=pq->curhighest+1;i<pq->levels;i++)
		assert(pq->lists[i].count == 0);
	for(int i=pq->levels;i<PRIQUEUE_LEVELS;i++)
		assert(pq->lists[i].count == 0);
	printk("\n%d<%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld:%ld>\n",
			current_thread->processor->id,
			pq->lists[0].count,
			pq->lists[1].count,
			pq->lists[2].count,
			pq->lists[3].count,
			pq->lists[4].count,
			pq->lists[5].count,
			pq->lists[6].count,
			pq->lists[7].count,
			pq->lists[8].count,
			pq->lists[9].count,
			pq->lists[10].count,
			pq->lists[11].count,
			pq->lists[12].count,
			pq->lists[13].count,
			pq->lists[14].count,
			pq->lists[15].count,
			pq->lists[16].count,
			pq->lists[17].count,
			pq->lists[18].count,
			pq->lists[19].count,
			pq->lists[20].count,
			pq->lists[21].count);
#endif
	spinlock_release(&pq->lock);
}

void *priqueue_pop(struct priqueue *pq)
{
	spinlock_acquire(&pq->lock);
	void *ret = NULL;
	for(int i=pq->curhighest;i>=0;i--) {
		if(pq->lists[i].count) {
			ret = linkedlist_remove_tail(&pq->lists[i]);

#if DEBUG_PRIQUEUE
			printk("%d Pop %d\n", current_thread->processor->id, i);
#endif

			if(i == pq->curhighest && pq->lists[i].count == 0)
				pq->curhighest--;
			break;
		}
		if(i == pq->curhighest && pq->lists[i].count == 0)
			pq->curhighest--;
	}
#if DEBUG_PRIQUEUE
	for(int i=pq->curhighest+1;i<pq->levels;i++)
		assert(pq->lists[i].count == 0);
	for(int i=pq->levels;i<PRIQUEUE_LEVELS;i++)
		assert(pq->lists[i].count == 0);
#endif
	spinlock_release(&pq->lock);
	return ret;
}

