#ifndef __MMU_H
#define __MMU_H

#define MAP_DEVICE   0x2 /* controls caching, if possible */
#define MAP_USER     0x4
#define MAP_WRITE    0x8
#define MAP_PRIVATE  0x10 /* is only accessed inside this address-space */
#define MAP_ACCESSED 0x20 /* has been accessed (if supported, set by processor) */
#define MAP_EXECUTE  0x40
#define MAP_DIRTY    0x80

#define FAULT_EXEC  0x1
#define FAULT_WRITE 0x2
#define FAULT_USER  0x4
#define FAULT_ERROR_PERM 0x10
#define FAULT_ERROR_PRES 0x20

#define MM_BUDDY_MIN_SIZE 0x1000

#include <arch-mmu.h>
#include <machine/memmap.h>
#include <slab.h>

struct vm_context {
	struct kobj_header _header;
	struct arch_vm_context arch;
};

extern struct kobj kobj_vm_context;

extern struct vm_context kernel_context;
void arch_mm_context_switch(struct vm_context *ctx);
uintptr_t mm_physical_allocate(size_t length, bool clear);
void mm_physical_deallocate(uintptr_t address);
void mm_init(void);
void mm_early_init(void);

void arch_mm_context_create(struct vm_context *ctx);
void arch_mm_context_init(struct vm_context *ctx);
void arch_mm_context_destroy(struct vm_context *ctx);

size_t arch_mm_page_size(int level);
bool arch_mm_virtual_map(struct vm_context *ctx, uintptr_t virt,
		uintptr_t phys, size_t pagesize, int flags);
uintptr_t arch_mm_virtual_unmap(struct vm_context *, uintptr_t virt);
bool arch_mm_virtual_getmap(struct vm_context *, uintptr_t virt, uintptr_t *phys, int *flags);
bool arch_mm_virtual_chattr(struct vm_context *ctx, uintptr_t virt, int flags);

void pmm_buddy_init(void);
void mm_fault_entry(uintptr_t address, int flags);
void mm_print_context(struct vm_context *ctx);

static inline uintptr_t mm_virtual_allocate(size_t length,
		bool clear)
{
	return mm_physical_allocate(length, clear) + PHYS_MAP_START;
}

static inline void mm_virtual_deallocate(uintptr_t addr)
{
	mm_physical_deallocate(addr - PHYS_MAP_START);
}

#endif

