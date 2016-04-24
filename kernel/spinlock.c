#include <spinlock.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <processor.h>
#include <interrupt.h>
void spinlock_create(struct spinlock *lock)
{
	lock->lock = ATOMIC_VAR_INIT(false);
	lock->interrupt = 0;
}

void spinlock_acquire(struct spinlock *lock)
{
	int interrupt = arch_interrupt_set(0);
	if(current_thread)
		current_thread->processor->preempt_disable++;
	while(atomic_exchange(&lock->lock, true))
		asm("pause"); //TODO: arch-dep
	lock->interrupt = interrupt;
}

void spinlock_release(struct spinlock *lock)
{
	int interrupt = lock->interrupt;
	atomic_store(&lock->lock, false);
	if(current_thread)
		current_thread->processor->preempt_disable--;
	arch_interrupt_set(interrupt);
}

