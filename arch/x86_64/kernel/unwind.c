#include <stdbool.h>
#include <debug.h>
#include <stdint.h>

bool arch_debug_unwind_frame(struct frame *frame)
{
	if(frame->fp == 0)
		return false;
	void *fp = (void *)frame->fp;
	frame->fp = *(uintptr_t *)(fp);
	frame->pc = *(uintptr_t *)((uintptr_t)fp + 8) - 5;
	return true;
}

