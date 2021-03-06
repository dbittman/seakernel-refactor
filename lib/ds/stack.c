#include <lib/stack.h>
#include <spinlock.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
static struct stack_elem __sentry;

void stack_create(struct stack *stack, int flags)
{
	spinlock_create(&stack->lock);
	memset(&__sentry, 0, sizeof(__sentry));
	stack->top = &__sentry;
	stack->flags = flags;
}

void stack_push(struct stack *stack, struct stack_elem *elem, void *obj)
{
	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_acquire(&stack->lock);
	
	stack->top->next = elem;
	elem->obj = obj;
	elem->next = &__sentry;
	elem->prev = stack->top;
	stack->top = elem;
	stack->count++;

	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_release(&stack->lock);
}

void stack_delete(struct stack *stack, struct stack_elem *elem)
{
	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_acquire(&stack->lock);
	
	assert(stack->count > 0);
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	if(stack->top == elem)
		stack->top = elem->prev;
	stack->count--;
	
	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_release(&stack->lock);
}

void *stack_pop(struct stack *stack)
{
	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_acquire(&stack->lock);

	if(!stack->count) {
		if(!(stack->flags & STACK_LOCKLESS))
			spinlock_release(&stack->lock);
		return NULL;
	}
	void *obj = stack->top->obj;
	stack->top->prev->next = &__sentry;
	stack->top = stack->top->prev;
	stack->count--;

	if(!(stack->flags & STACK_LOCKLESS))
		spinlock_release(&stack->lock);
	return obj;
}

