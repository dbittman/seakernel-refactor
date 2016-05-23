#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
struct arch_vm_context {
	uintptr_t pml4_phys;
};

struct vm_context;
void arch_mm_init(struct vm_context *ctx);
uintptr_t arch_mm_physical_allocate(size_t size, bool clear);

#define MMU_NUM_PAGESIZE_LEVELS 2

#define MMU_PTE_GLOBAL   (1 << 8)
#define MMU_PTE_LARGE    (1 << 7)
#define MMU_PTE_DIRTY    (1 << 6)
#define MMU_PTE_ACCESSED (1 << 5)
#define MMU_PTE_NOCACHE  (1 << 4)
#define MMU_PTE_WRITETHR (1 << 3)
#define MMU_PTE_USER     (1 << 2)
#define MMU_PTE_WRITE    (1 << 1)
#define MMU_PTE_PRESENT  (1 << 0)
#define MMU_PTE_NOEXEC   (1ull << 63)

#define MMU_PTE_PHYS_MASK 0x7FFFFFFFFFFFF000
__attribute__((const,pure)) static inline size_t arch_mm_page_size(int level)
{
	if(level == 0)
		return 0x1000; //4K
	else if(level == 1)
		return 2 * 1024 * 1024; //2M
	else
		return 1024 * 1024 * 1024; //1G
}


