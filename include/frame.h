#pragma once

struct frame {
	size_t framenr, pagenr;
	_Atomic int count;
};


struct frame *frame_get_from_address(uintptr_t phys);
void frame_acquire(uintptr_t phys);
uintptr_t frame_allocate(void);
void frame_release(uintptr_t phys);

