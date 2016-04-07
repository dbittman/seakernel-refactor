#include <mmu.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <machine/memmap.h>
#include <assert.h>
#include <mmu-bits.h>
#include <printk.h>
#include <string.h>
#include <thread.h>
extern unsigned long long kernel_translation_table[];
/* TODO (major) [dbittman]: Make these atomic */

size_t arch_mm_page_size(int level)
{
	/* TODO [dbittman]: support different page sizes */
	if(level == 0)
		return 64 * 1024;
	return 512 * 1024 * 1024;
}

static int __convert_attr_to_flags(uint64_t attr)
{
	int flags = 0;
	if(attr & MMU_PTE_ATTR_AF)
		flags |= MAP_ACCESSED;
	if((attr & MMU_PTE_ATTR_ATTR_INDEX_MASK) == MMU_PTE_ATTR_DEVICE)
		flags |= MAP_DEVICE;
	if(attr & MMU_PTE_ATTR_NON_GLOBAL)
		flags |= MAP_PRIVATE;
	if(attr & MMU_PTE_ATTR_PXN || attr & MMU_PTE_ATTR_UXN)
		flags |= MAP_EXECUTE;
	switch(attr & MMU_PTE_ATTR_AP_MASK) {
		case MMU_PTE_ATTR_AP_P_RW_U_RW:
			flags |= MAP_USER | MAP_WRITE;
			break;
		case MMU_PTE_ATTR_AP_P_RO_U_RO:
			flags |= MAP_USER;
			break;
		case MMU_PTE_ATTR_AP_P_RW_U_NA:
			flags |= MAP_WRITE;
			break;
	}
	return flags;
}

static uint64_t __convert_flags_to_attr(int flags)
{
	uint64_t entry_attr = MMU_PTE_ATTR_UXN | MMU_PTE_ATTR_PXN;
	if(flags & MAP_USER) {
		if(flags & MAP_WRITE)
			entry_attr |= MMU_PTE_ATTR_AP_P_RW_U_RW;
		else
			entry_attr |= MMU_PTE_ATTR_AP_P_RO_U_RO;
		if(flags & MAP_EXECUTE)
			entry_attr &= ~MMU_PTE_ATTR_UXN;
	} else {
		if(flags & MAP_WRITE)
			entry_attr |= MMU_PTE_ATTR_AP_P_RW_U_NA;
		else
			entry_attr |= MMU_PTE_ATTR_AP_P_RO_U_NA;
		if(flags & MAP_EXECUTE)
			entry_attr &= ~MMU_PTE_ATTR_PXN;
	}
	if(flags & MAP_PRIVATE)
		entry_attr |= MMU_PTE_ATTR_NON_GLOBAL;
	if(flags & MAP_DEVICE)
		entry_attr |= MMU_PTE_ATTR_DEVICE | MMU_PTE_ATTR_SH_OUTER_SHAREABLE;
	else
		entry_attr |= MMU_PTE_ATTR_NORMAL_MEMORY | MMU_PTE_ATTR_SH_INNER_SHAREABLE;
	if(flags & MAP_ACCESSED)
		entry_attr |= MMU_PTE_ATTR_AF;
	return entry_attr;
}

bool arch_mm_virtual_map(struct vm_context *ctx, uintptr_t virt,
		uintptr_t phys, size_t pagesize, int flags)
{
	assert(pagesize == arch_mm_page_size(0) || pagesize == arch_mm_page_size(1));
	int l1_idx = (virt & 0xFC0000000000ULL) >> 42;
	int l2_idx = (virt & 0x3FFE0000000ULL) >> 29;
	int l3_idx = (virt & 0x1FFF0000ULL) >> 16;
	uint64_t attrs = __convert_flags_to_attr(flags);
	uint64_t *level1;
	if(virt & 0x0FFF000000000000)
		level1 = (uint64_t *)(ctx->arch.kernel_tt + PHYS_MAP_START);
	else
		level1 = (uint64_t *)(ctx->arch.user_tt + PHYS_MAP_START);
	if(!level1[l1_idx]) {
		level1[l1_idx] = mm_physical_allocate(0x10000, true) | MMU_PTE_L012_DESCRIPTOR_TABLE;
	}
	uint64_t *level2 = (uint64_t *)((level1[l1_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
	if(pagesize == arch_mm_page_size(0)) {
		if(!level2[l2_idx]) {
			level2[l2_idx] = mm_physical_allocate(0x10000, true) | MMU_PTE_L012_DESCRIPTOR_TABLE;
		}
		uint64_t *level3 = (uint64_t *)((level2[l2_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
		if(level3[l3_idx])
			return false;
		level3[l3_idx] = phys | attrs | MMU_PTE_L3_DESCRIPTOR_PAGE;
	} else {
		if(level2[l2_idx])
			return false;
		level2[l2_idx] = phys | attrs | MMU_PTE_L012_DESCRIPTOR_BLOCK;
	}
	return true;
}

uintptr_t arch_mm_virtual_unmap(struct vm_context *ctx, uintptr_t virt)
{
	int l1_idx = (virt & 0xFC0000000000ULL) >> 42;
	int l2_idx = (virt & 0x3FFE0000000ULL) >> 29;
	int l3_idx = (virt & 0x1FFF0000ULL) >> 16;
	uint64_t *level1;
	uintptr_t ret = 0;
	if(virt & 0x0FFF000000000000)
		level1 = (uint64_t *)(ctx->arch.kernel_tt + PHYS_MAP_START);
	else
		level1 = (uint64_t *)(ctx->arch.user_tt + PHYS_MAP_START);
	if(!level1[l1_idx]) {
		return 0;
	}
	uint64_t *level2 = (uint64_t *)((level1[l1_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
	if(!level2[l2_idx]) {
		return 0;
	}
	if((level2[l2_idx] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
		uint64_t *level3 = (uint64_t *)((level2[l2_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
		if(!level3[l3_idx])
			return 0;
		ret = level3[l3_idx] & MMU_PTE_OUTPUT_ADDR_MASK;
		level3[l3_idx] = 0;
	} else {
		if(!level2[l2_idx])
			return 0;
		ret = level2[l2_idx] & MMU_PTE_OUTPUT_ADDR_MASK;
		level2[l2_idx] = 0;
	}

	return ret;
}

bool arch_mm_virtual_getmap(struct vm_context *ctx, uintptr_t virt, uintptr_t *phys, int *flags)
{
	int l1_idx = (virt & 0xFC0000000000ULL) >> 42;
	int l2_idx = (virt & 0x3FFE0000000ULL) >> 29;
	int l3_idx = (virt & 0x1FFF0000ULL) >> 16;
	uint64_t *level1;
	if(virt & 0x0FFF000000000000)
		level1 = (uint64_t *)(ctx->arch.kernel_tt + PHYS_MAP_START);
	else
		level1 = (uint64_t *)(ctx->arch.user_tt + PHYS_MAP_START);
	if(!level1[l1_idx]) {
		return false;
	}
	uint64_t *level2 = (uint64_t *)((level1[l1_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
	if(!level2[l2_idx]) {
		if(phys)
			*phys = level2[l2_idx] & MMU_PTE_OUTPUT_ADDR_MASK;
		if(flags)
			*flags = __convert_attr_to_flags(level2[l2_idx] & ~MMU_PTE_OUTPUT_ADDR_MASK);
		return false;
	}
	if((level2[l2_idx] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
		uint64_t *level3 = (uint64_t *)((level2[l2_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
		if(phys)
			*phys = level3[l3_idx] & MMU_PTE_OUTPUT_ADDR_MASK;
		if(flags)
			*flags = __convert_attr_to_flags(level3[l3_idx] & ~MMU_PTE_OUTPUT_ADDR_MASK);
		if(!level3[l3_idx])
			return false;
	} else {
		if(phys)
			*phys = level2[l2_idx] & MMU_PTE_OUTPUT_ADDR_MASK;
		if(flags)
			*flags = __convert_attr_to_flags(level2[l2_idx] & ~MMU_PTE_OUTPUT_ADDR_MASK);
		if(!level2[l2_idx])
			return false;
	}

	return true;
}

bool arch_mm_virtual_chattr(struct vm_context *ctx, uintptr_t virt, int flags)
{
	uint64_t attr = __convert_flags_to_attr(flags);
	int l1_idx = (virt & 0xFC0000000000ULL) >> 42;
	int l2_idx = (virt & 0x3FFE0000000ULL) >> 29;
	int l3_idx = (virt & 0x1FFF0000ULL) >> 16;
	uint64_t *level1;
	if(virt & 0x0FFF000000000000)
		level1 = (uint64_t *)(ctx->arch.kernel_tt + PHYS_MAP_START);
	else
		level1 = (uint64_t *)(ctx->arch.user_tt + PHYS_MAP_START);
	if(!level1[l1_idx]) {
		return false;
	}
	uint64_t *level2 = (uint64_t *)((level1[l1_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
	if(!level2[l2_idx]) {
		return false;
	}
	if((level2[l2_idx] & MMU_PTE_DESCRIPTOR_MASK) == MMU_PTE_L012_DESCRIPTOR_TABLE) {
		uint64_t *level3 = (uint64_t *)((level2[l2_idx] & ~MMU_PTE_DESCRIPTOR_MASK) + PHYS_MAP_START);
		if(!level3[l3_idx])
			return false;
		level3[l3_idx] = (level3[l3_idx] & MMU_PTE_OUTPUT_ADDR_MASK) | attr | MMU_PTE_L3_DESCRIPTOR_PAGE;
		asm("tlbi vmalle1is; isb; dsb sy"); /* TODO (major) [dbittman]: only invalidate what we need */
	} else {
		level2[l2_idx] = (level2[l2_idx] & MMU_PTE_OUTPUT_ADDR_MASK) | attr | MMU_PTE_L012_DESCRIPTOR_BLOCK;
		asm("tlbi vmalle1is; isb; dsb sy");
	}
	return true;
}

void arch_mm_context_create(struct vm_context *ctx)
{
	ctx->arch.kernel_tt = (uintptr_t)&kernel_translation_table - (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
	ctx->arch.user_tt = mm_physical_allocate(arch_mm_page_size(0), true);
}

void arch_mm_context_init(struct vm_context *ctx)
{
	memset((void *)ctx->arch.user_tt, 0, arch_mm_page_size(0));
}

void arch_mm_context_destroy(struct vm_context *ctx)
{
	mm_physical_deallocate(ctx->arch.user_tt);
}

void aarch64_mm_pagefault(uintptr_t addr, int reason, bool write, bool exec, bool userspace)
{
	int fault_cause = reason & 0x3C;
	switch(fault_cause) {
		int flags;
		case 8:
			printk("[fault]: access flag at %lx: ok\n", addr);
			bool r = arch_mm_virtual_chattr(current_context, addr & 0xFFFFFFFFFFFF0000, MAP_WRITE | MAP_ACCESSED);
			assert(r);
			break;
		case 4: case 12:
			flags = 0;
			if(write)
				flags |= FAULT_WRITE;
			if(exec)
				flags |= FAULT_EXEC;
			if(userspace)
				flags |= FAULT_USER;
			if(fault_cause == 4)
				flags |= FAULT_ERROR_PRES;
			else
				flags |= FAULT_ERROR_PERM;
			mm_fault_entry(addr, flags);
			break;
		default:
			/* TODO [dbittman]: userspace handled differently here */
			panic(0, "unhandled page fault: %lx", addr);
	}
}

void arch_mm_init(struct vm_context *ctx)
{
	ctx->arch.kernel_tt = (uintptr_t)&kernel_translation_table - (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
	ctx->arch.user_tt = (uintptr_t)&kernel_translation_table - (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
	ctx->arch.user_tt = mm_physical_allocate(arch_mm_page_size(0), true);
	arch_mm_context_switch(ctx);
}

void arch_mm_context_switch(struct vm_context *ctx)
{
	asm volatile("msr TTBR0_EL1, %0" :: "r"(ctx->arch.user_tt));
	asm volatile("msr TTBR1_EL1, %0" :: "r"(ctx->arch.kernel_tt));
}

