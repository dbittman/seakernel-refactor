#include <interrupt.h>
#include <stddef.h>
#include <stdatomic.h>
#include <thread.h>
#include <printk.h>
#include <slab.h>

struct kobj kobj_exception_frame = KOBJ_DEFAULT(exception_frame);

void interrupt_push_frame(struct arch_exception_frame *af)
{
	struct exception_frame *frame = kobj_allocate(&kobj_exception_frame);
	memcpy(&frame->arch, af, sizeof(*af));
	linkedlist_insert(&current_thread->saved_exception_frames, &frame->node, frame);
}

struct exception_frame *interrupt_pop_frame(void)
{
	return linkedlist_remove_head(&current_thread->saved_exception_frames);
}

void (*_Atomic hand[MAX_INTERRUPTS][MAX_HANDLERS])();

void interrupt_init(void)
{
	for(int i=0;i<MAX_INTERRUPTS;i++) {
		for(int j=0;j<MAX_HANDLERS;j++) {
			hand[i][j] = NULL;
		}
	}
}

void interrupt_entry(int vector, int flags)
{
	for(int i=0;i<MAX_HANDLERS;i++) {
		void (*f)(int) = atomic_load(&hand[vector][i]);
		if(f) {
			f(flags);
		}
	}
	if(current_thread && (current_thread->flags & THREAD_RESCHEDULE)) {
		preempt();
	}
}

int interrupt_register(int vector, void (*f)(int flags))
{
	for(int i=0;i<MAX_HANDLERS;i++) {
		void (*exp)() = NULL;
		if(atomic_compare_exchange_strong(&hand[vector][i], &exp, f))
			return i;
	}
	return -1;
}

