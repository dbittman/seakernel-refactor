#include <lib/heap.h>
#include <stdbool.h>
#include <assert.h>
#include <printk.h>
#include <string.h>
#define DEBUG_HEAP 1

void heap_create(struct heap *heap, int (*comp)(void *, void *), enum heap_type type)
{
	spinlock_create(&heap->lock);
	heap->count = 0;
	heap->type = type;
	heap->compare = comp;
	heap->end = heap->root = NULL;
}
