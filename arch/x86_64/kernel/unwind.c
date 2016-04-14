#include <stdbool.h>
#include <debug.h>
#include <stdint.h>
#include <mmu.h>

#ifdef __clang__
__attribute__((no_sanitize("alignment")))
#else
__attribute__((no_sanitize_undefined))
#endif
bool arch_debug_unwind_frame(struct frame *frame)
{
	if(frame->fp == 0 || !(frame->fp >= PHYS_MAP_START))
		return false;
	void *fp = (void *)frame->fp;
	frame->fp = *(uintptr_t *)(fp);
	frame->pc = *(uintptr_t *)((uintptr_t)fp + 8) - 5;
	return true;
}

