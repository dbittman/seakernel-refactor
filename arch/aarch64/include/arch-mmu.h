#ifndef __ARCH_MMU_H
#define __ARCH_MMU_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct arch_vm_context {
	uintptr_t kernel_tt, user_tt;
};

struct vm_context;
void arch_mm_init(struct vm_context *ctx);
uintptr_t arch_mm_physical_allocate(size_t size, bool clear);
#endif

