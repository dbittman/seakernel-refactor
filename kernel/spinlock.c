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
	processor_disable_preempt();
	while(atomic_exchange(&lock->lock, true))
		;
	lock->interrupt = interrupt;
}

void spinlock_release(struct spinlock *lock)
{
	int interrupt = lock->interrupt;
	atomic_store(&lock->lock, false);
	processor_enable_preempt();
	arch_interrupt_set(interrupt);
}

