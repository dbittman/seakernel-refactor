#include <interrupt.h>
#include <stddef.h>
#include <stdatomic.h>
#include <thread.h>
#include <printk.h>
#include <slab.h>

struct kobj kobj_exception_frame = KOBJ_DEFAULT(exception_frame);

void interrupt_push_frame(struct arch_exception_frame *af, struct sigaction *action)
{
	struct exception_frame *frame = kobj_allocate(&kobj_exception_frame);
	memcpy(&frame->arch, af, sizeof(*af));
	memcpy(&frame->mask, &current_thread->sigmask, sizeof(frame->mask));
	sigorset(&current_thread->sigmask, &current_thread->sigmask, (sigset_t *)&action->mask);
	linkedlist_insert(&current_thread->saved_exception_frames, &frame->node, frame);
}

struct exception_frame *interrupt_pop_frame(void)
{
	struct exception_frame *frame = linkedlist_remove_head(&current_thread->saved_exception_frames);
	memcpy(&current_thread->sigmask, &frame->mask, sizeof(sigset_t));
	return frame;
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
		void (*f)(int, int) = atomic_load(&hand[vector][i]);
		if(f) {
			f(vector, flags);
		}
	}
}

int interrupt_register(int vector, void (*f)(int vec, int flags))
{
	for(int i=0;i<MAX_HANDLERS;i++) {
		void (*exp)() = NULL;
		if(atomic_compare_exchange_strong(&hand[vector][i], &exp, f))
			return i;
	}
	return -1;
}

