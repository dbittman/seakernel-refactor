#pragma once
#include <stdint.h>

static inline uint32_t arch_interrupt_set(int st)
{
	long old;
	asm volatile("pushfq; pop %0" : "=r"(old) :: "memory");
	if(st)
		asm volatile("sti");
	else
		asm volatile("cli");
	return old & (1 << 9);
}

#define MAX_INTERRUPTS 256
