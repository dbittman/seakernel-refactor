#pragma once

#include <spinlock.h>
#include <mmu.h>
#include <assert.h>
#include <system.h>

struct arena {
	void *start;
	struct spinlock lock;
};

struct arena_node {
	size_t used;
	size_t len;
	void *next;
};

static inline void arena_create(struct arena *arena)
{
	spinlock_create(&arena->lock);
	arena->start = (void *)mm_virtual_allocate(arch_mm_page_size(0), true);
	struct arena_node *node = arena->start;
	node->len = arch_mm_page_size(0);
	node->used = sizeof(struct arena_node);
}

static inline void *arena_allocate(struct arena *arena, size_t length)
{
	spinlock_acquire(&arena->lock);
	
	length = (length & ~15) + 16;

	struct arena_node *node = arena->start, *prev = NULL;
	while(node && (node->used + length >= node->len)) {
		prev = node;
		node = node->next;
	}

	if(!node) {
		assert(prev != NULL);
		node = prev->next = (void *)mm_virtual_allocate(__round_up_pow2(length * 2), true);
		node->len = __round_up_pow2(length * 2);
		node->used = sizeof(struct arena_node);
	}
	
	void *ret = (void *)((uintptr_t)node + node->used);
	node->used += length;

	spinlock_release(&arena->lock);
	return ret;
}

static inline void arena_destroy(struct arena *arena)
{
	struct arena_node *node = arena->start, *next;
	while(node) {
		next = node->next;
		mm_virtual_deallocate((uintptr_t)node);
		node = next;
	}
}

