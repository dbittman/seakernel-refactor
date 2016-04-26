#ifndef __MUTEX_H
#define __MUTEX_H

#include <stdatomic.h>
#include <stdbool.h>
#include <blocklist.h>

struct thread;
struct mutex {
	_Atomic int lock;
	struct blocklist wait;
#if CONFIG_DEBUG
	struct thread * _Atomic owner;
#endif
};

void mutex_acquire(struct mutex *);
void mutex_release(struct mutex *);
void mutex_create(struct mutex *);

#endif

