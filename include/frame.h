#pragma once
#include <lib/hash.h>

#define FRAME_PERSIST 1
#define FRAME_NOCLEAR 2
#define FRAME_ZEROCOUNT 4
struct frame {
	_Atomic int flags;
	_Atomic long count;
};

uintptr_t frame_get_physical(struct frame *);
struct frame *frame_get_from_address(uintptr_t phys);
void frame_acquire(uintptr_t phys);
uintptr_t frame_allocate(int level, int flags);
long frame_release(uintptr_t phys);

