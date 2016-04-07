#ifndef __THREAD_ARCH_H
#define __THREAD_ARCH_H

#include <stdint.h>

struct arch_thread {
	uint8_t fpu_data[512];
	uint64_t fpcr;
	uint64_t fpsr;
};

__attribute__((const)) static inline struct thread *arch_thread_get_current(void)
{
	uint64_t id;
	asm volatile("mrs %0, TPIDR_EL1" : "=r"(id));
	return (struct thread *)id;
}

#endif

