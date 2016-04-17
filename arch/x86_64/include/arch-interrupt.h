#pragma once
#include <stdint.h>

struct __attribute__((packed)) arch_exception_frame
{
	uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi;
	uint64_t int_no, err_code;
	uint64_t rip, cs, rflags, userrsp, ss;
};

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
