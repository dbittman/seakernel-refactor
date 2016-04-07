#ifndef __SEA_LIB_STACK_H
#define __SEA_LIB_STACK_H

#include <stdbool.h>
#include <spinlock.h>
#include <stddef.h>
#define STACK_LOCKLESS 1

struct stack_elem {
	void *obj;
	struct stack_elem *next, *prev;
};

struct stack {
	int flags;
	_Atomic size_t count;
	struct stack_elem *top;
	struct spinlock lock;
};

static inline bool stack_is_empty(struct stack *stack)
{
	return stack->count == 0;
}

void stack_create(struct stack *stack, int flags);
void stack_push(struct stack *stack, struct stack_elem *elem, void *obj);
void *stack_pop(struct stack *stack);
void stack_delete(struct stack *stack, struct stack_elem *elem);

#endif

