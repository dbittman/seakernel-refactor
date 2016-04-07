#ifndef __SPINLOCK_H
#define __SPINLOCK_H
#include <stdbool.h>
#include <stdatomic.h>
struct spinlock {
	_Atomic bool lock;
	int interrupt;
};

void spinlock_create(struct spinlock *);
void spinlock_acquire(struct spinlock *);
void spinlock_release(struct spinlock *);

#define SPINLOCK_INIT { 0, 0 }

#endif

