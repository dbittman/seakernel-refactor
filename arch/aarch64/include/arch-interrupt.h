#ifndef __ARCH_INTERRUPT_H
#define __ARCH_INTERRUPT_H
#include <stdint.h>
static inline uint32_t arch_interrupt_set(int st)
{
	uint64_t oldmask;
	asm volatile("mrs %0, DAIF" : "=r"(oldmask));
	asm volatile("msr DAIF, %0" :: "r"(st ? 0ul : 1ul << 6 | 1ul << 7));
	if(oldmask & (1 << 6))
		return 0;
	return 1;
}

struct exception_frame {
	uint64_t x0;
	uint64_t x1;
	uint64_t x2;
	uint64_t x3;
	uint64_t x4;
	uint64_t x5;
	uint64_t x6;
	uint64_t x7;
	uint64_t x8;
	uint64_t x9;
	uint64_t x10;
	uint64_t x11;
	uint64_t x12;
	uint64_t x13;
	uint64_t x14;
	uint64_t x15;
	uint64_t x16;
	uint64_t x17;
	uint64_t x18;
	uint64_t x19;
	uint64_t x20;
	uint64_t x21;
	uint64_t x22;
	uint64_t x23;
	uint64_t x24;
	uint64_t x25;
	uint64_t x26;
	uint64_t x27;
	uint64_t x28;
	uint64_t x29;
	uint64_t x30;
	uint64_t elr;
	uint64_t spsr;
};

#define MAX_INTERRUPTS 1024

#endif

