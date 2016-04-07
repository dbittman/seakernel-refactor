#pragma once

#include <stddef.h>
#include <spinlock.h>

struct heapnode {
	struct heapnode *left, *right, *parent;
	void *data;
};

enum heap_type {
	heap_type_max,
	heap_type_min,
};

struct heap {
	struct heapnode *root, *end;
	_Atomic size_t count;
	struct spinlock lock;
	enum heap_type type;
	int (*compare)(void *, void *);
};

static inline size_t heap_count(struct heap *heap)
{
	return heap->count;
}

void *heap_pop(struct heap *heap);
void heap_insert(struct heap *heap, struct heapnode *node, void *data);
void heap_create(struct heap *heap, int (*comp)(void *, void *), enum heap_type type);
