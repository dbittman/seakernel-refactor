#include <spinlock.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <processor.h>
#include <interrupt.h>
void spinlock_create(struct spinlock *lock)
{
	memset(lock, 0, sizeof(*lock));
}

void spinlock_acquire(struct spinlock *lock)
{
	int interrupt = arch_interrupt_set(0);
	if(current_thread && current_thread->processor) {
		int r = current_thread->processor->preempt_disable++;
		assert(r >= 0);
	}
#if CONFIG_DEBUG
	int timeout = 100000000;
#endif
	while(atomic_flag_test_and_set(&lock->lock)) {
#if CONFIG_DEBUG
		if(--timeout == 0) {
			panic(0, "possible deadlock");
		}
#endif
		asm("pause"); //TODO: arch-dep
	}
#if CONFIG_DEBUG
	if(current_thread)
		current_thread->held_spinlocks++;
#endif
	lock->interrupt = interrupt;
}

void spinlock_release(struct spinlock *lock)
{
	int interrupt = lock->interrupt;
#if CONFIG_DEBUG
	if(current_thread)
		current_thread->held_spinlocks--;
#endif
	atomic_flag_clear(&lock->lock);
	if(current_thread && current_thread->processor) {
		int r = current_thread->processor->preempt_disable--;
		assert(r > 0);
	}
	arch_interrupt_set(interrupt);
}

