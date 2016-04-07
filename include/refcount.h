#pragma once
#include <stdatomic.h>
#include <stdbool.h>
typedef _Atomic unsigned long refcount_t;

static inline void refcount_get(refcount_t *refs)
{
	atomic_fetch_add(refs, 1);
}

static inline bool refcount_dec(refcount_t *refs, void (*destroy)(void *), void *obj)
{
	if(atomic_fetch_sub(refs, 1) == 1)
		destroy(obj);
}

