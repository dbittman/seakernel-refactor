#pragma once
#include <thread-bits.h>
#include <stdbool.h>
struct arch_thread {
	_Alignas(16) uint8_t fpu_data[512];
	uintptr_t fs;
	bool usedfpu;
};

__attribute__((const)) static inline struct thread *arch_thread_get_current(void)
{
	uint64_t stack;
	asm("mov %%rsp, %0" : "=r"(stack));
	return *(struct thread **)(stack & ~(KERNEL_STACK_SIZE - 1));
}

