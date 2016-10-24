#include <mmu.h>
#include <processor.h>
#include <process.h>
#include <thread.h>
void x86_64_processor_send_ipi(long dest, int signal);
extern uintptr_t *initial_pml4;

/* TODO (major) [dbittman]: Make these atomic */
/* TODO (major) [dbittman]: Invalidation */
/* TODO (major) [dbittman]: PAGE_PRESENT */
#define PML4_IDX(v) ((v >> 39) & 0x1FF)
#define PDPT_IDX(v) ((v >> 30) & 0x1FF)
#define PD_IDX(v)   ((v >> 21) & 0x1FF)
#define PT_IDX(v)   ((v >> 12) & 0x1FF)

static void tlb_shootdown(void)
{
	x86_64_processor_send_ipi(PROCESSOR_IPI_DEST_OTHERS, PROCESSOR_IPI_SHOOTDOWN);
}

static void __invalidate(uintptr_t virt)
{
	asm volatile("invlpg (%%rax)" :: "a"(virt) : "memory");
	tlb_shootdown();
#if 0
	if(current_thread && current_thread->process && current_thread->process->threads.count > 1) {
		if(virt >= USER_TLS_REGION_END || virt < USER_TLS_REGION_START)
			tlb_shootdown();
	} else if(virt >= USER_REGION_END && current_thread) {
		tlb_shootdown();
	}
#endif

}

static int __convert_attr_to_flags(uint64_t attr)
{
	int flags = 0;

	if(attr & MMU_PTE_ACCESSED)
		flags |= MAP_ACCESSED;
	if((attr & MMU_PTE_WRITETHR) && (attr & MMU_PTE_NOCACHE))
		flags |= MAP_DEVICE;
	/* TODO: global page flag in cr4 */
	if(!(attr & MMU_PTE_GLOBAL))
		flags |= MAP_PRIVATE;
	if(!(attr & MMU_PTE_NOEXEC))
		flags |= MAP_EXECUTE;
	if(attr & MMU_PTE_USER)
		flags |= MAP_USER;
	if(attr & MMU_PTE_WRITE)
		flags |= MAP_WRITE;
	return flags;
}

static uint64_t __convert_flags_to_attr(int flags)
{
	uint64_t entry_attr = 0;
	if(!(flags & MAP_PRIVATE))
		entry_attr |= MMU_PTE_GLOBAL;
	if(flags & MAP_USER)
		entry_attr |= MMU_PTE_USER;
	if(flags & MAP_WRITE)
		entry_attr |= MMU_PTE_WRITE;
	if(flags & MAP_DEVICE)
		entry_attr |= MMU_PTE_NOCACHE | MMU_PTE_WRITETHR;
	if(!(flags & MAP_EXECUTE))
		entry_attr |= MMU_PTE_NOEXEC;
	if(flags & MAP_ACCESSED)
		entry_attr |= MMU_PTE_ACCESSED;
	if(flags & MAP_DIRTY)
		entry_attr |= MMU_PTE_DIRTY;
	return entry_attr;
}

#include <printk.h>
bool arch_mm_virtual_map(struct vm_context *ctx, uintptr_t virt,
		uintptr_t phys, size_t pagesize, int flags)
{
	int pml4_idx = PML4_IDX(virt);
	int pdpt_idx = PDPT_IDX(virt);
	int pd_idx = PD_IDX(virt);
	int pt_idx = PT_IDX(virt);

	uint64_t attr = __convert_flags_to_attr(flags);

	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	if(!pml4[pml4_idx]) {
		pml4[pml4_idx] = (uintptr_t)mm_physical_allocate(0x1000, true) | MMU_PTE_PRESENT | MMU_PTE_WRITE | MMU_PTE_USER;
	}
	uintptr_t *pdpt = (uintptr_t *)((pml4[pml4_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(pagesize == arch_mm_page_size(2)) {
		if(pdpt[pdpt_idx])
			return false;
		pdpt[pdpt_idx] = phys | attr | MMU_PTE_PRESENT | MMU_PTE_LARGE;
		__invalidate(virt);
	} else {
		if(!pdpt[pdpt_idx]) {
			pdpt[pdpt_idx] = (uintptr_t)mm_physical_allocate(0x1000, true) | MMU_PTE_PRESENT | MMU_PTE_WRITE | MMU_PTE_USER;
		}
		uintptr_t *pd = (uintptr_t *)((pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
		if(pagesize == arch_mm_page_size(1)) {
			if(pd[pd_idx])
				return false;
			pd[pd_idx] = phys | attr | MMU_PTE_PRESENT | MMU_PTE_LARGE;
			__invalidate(virt);
		} else {
			if(!pd[pd_idx]) {
				pd[pd_idx] = (uintptr_t)mm_physical_allocate(0x1000, true) | MMU_PTE_PRESENT | MMU_PTE_WRITE | MMU_PTE_USER;
			}
			uintptr_t *pt = (uintptr_t *)((pd[pd_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
			if(pt[pt_idx])
				return false;
			pt[pt_idx] = phys | attr | MMU_PTE_PRESENT;
			__invalidate(virt);
		}
	}

	return true;
}

uintptr_t arch_mm_virtual_unmap(struct vm_context *ctx, uintptr_t virt)
{
	int pml4_idx = PML4_IDX(virt);
	int pdpt_idx = PDPT_IDX(virt);
	int pd_idx = PD_IDX(virt);
	int pt_idx = PT_IDX(virt);

	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	if(!pml4[pml4_idx])
		return 0;

	uintptr_t *pdpt = (uintptr_t *)((pml4[pml4_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pdpt[pdpt_idx])
		return 0;
	if(pdpt[pdpt_idx] & MMU_PTE_LARGE) {
		uintptr_t ret = pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK;
		pdpt[pdpt_idx] = 0;
		__invalidate(virt);
		return ret;
	}
	
	uintptr_t *pd = (uintptr_t *)((pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pd[pd_idx])
		return 0;
	if(pd[pd_idx] & MMU_PTE_LARGE) {
		uintptr_t ret = pd[pd_idx] & MMU_PTE_PHYS_MASK;
		pd[pd_idx] = 0;
		__invalidate(virt);
		return ret;
	}

	uintptr_t *pt = (uintptr_t *)((pd[pd_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	uintptr_t ret = pt[pt_idx] & MMU_PTE_PHYS_MASK;
	pt[pt_idx] = 0;
	__invalidate(virt);
	return ret;
}

bool arch_mm_virtual_getmap(struct vm_context *ctx, uintptr_t virt, uintptr_t *phys, int *flags, size_t *size)
{
	int pml4_idx = PML4_IDX(virt);
	int pdpt_idx = PDPT_IDX(virt);
	int pd_idx = PD_IDX(virt);
	int pt_idx = PT_IDX(virt);

	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	if(!pml4[pml4_idx])
		return false;

	uintptr_t *pdpt = (uintptr_t *)((pml4[pml4_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pdpt[pdpt_idx]) {
		return false;
	}
	if(pdpt[pdpt_idx] & MMU_PTE_LARGE) {
		if(phys)
			*phys = pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK;
		if(flags)
			*flags = __convert_attr_to_flags(pdpt[pdpt_idx] & ~MMU_PTE_PHYS_MASK);
		if(size)
			*size = arch_mm_page_size(2);
		return pdpt[pdpt_idx] != 0;
	}
	
	uintptr_t *pd = (uintptr_t *)((pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pd[pd_idx])
		return false;
	if(pd[pd_idx] & MMU_PTE_LARGE) {
		if(phys)
			*phys = pd[pd_idx] & MMU_PTE_PHYS_MASK;
		if(flags)
			*flags = __convert_attr_to_flags(pd[pd_idx] & ~MMU_PTE_PHYS_MASK);
		if(size)
			*size = arch_mm_page_size(1);
		return pd[pd_idx] != 0;
	}

	uintptr_t *pt = (uintptr_t *)((pd[pd_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(phys)
		*phys = pt[pt_idx] & MMU_PTE_PHYS_MASK;
	if(flags)
		*flags = __convert_attr_to_flags(pt[pt_idx] & ~MMU_PTE_PHYS_MASK);
	if(size)
		*size = arch_mm_page_size(0);
	return pt[pt_idx] != 0;
}

bool arch_mm_virtual_chattr(struct vm_context *ctx, uintptr_t virt, int flags)
{
	int pml4_idx = PML4_IDX(virt);
	int pdpt_idx = PDPT_IDX(virt);
	int pd_idx = PD_IDX(virt);
	int pt_idx = PT_IDX(virt);

	uint64_t attr = __convert_flags_to_attr(flags);

	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	if(!pml4[pml4_idx])
		return false;

	uintptr_t *pdpt = (uintptr_t *)((pml4[pml4_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pdpt[pdpt_idx])
		return false;
	if(pdpt[pdpt_idx] & MMU_PTE_LARGE) {
		pdpt[pdpt_idx] = (pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK) | attr | MMU_PTE_PRESENT | MMU_PTE_LARGE;
		__invalidate(virt);
		return true;
	}
	
	uintptr_t *pd = (uintptr_t *)((pdpt[pdpt_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pd[pd_idx])
		return false;
	if(pd[pd_idx] & MMU_PTE_LARGE) {
		pd[pd_idx] = (pd[pd_idx] & MMU_PTE_PHYS_MASK) | attr | MMU_PTE_PRESENT | MMU_PTE_LARGE;
		__invalidate(virt);
		return true;
	}

	uintptr_t *pt = (uintptr_t *)((pd[pd_idx] & MMU_PTE_PHYS_MASK) + PHYS_MAP_START);
	if(!pt[pt_idx])
		return false;
	pt[pt_idx] = (pt[pt_idx] & MMU_PTE_PHYS_MASK) | attr | MMU_PTE_PRESENT;
	__invalidate(virt);
	return true;
}

void arch_mm_context_create(struct vm_context *ctx)
{
	/* map higher half of address for the kernel */
	ctx->arch.pml4_phys = mm_physical_allocate(0x1000, true);
	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	uintptr_t *kern_pml4 = (uintptr_t *)(kernel_context.arch.pml4_phys + PHYS_MAP_START);
	for(int i=0;i<256;i++)
		pml4[i] = 0;
	for(int i=256;i<512;i++) {
		pml4[i] = kern_pml4[i];
	}
}

void arch_mm_context_init(struct vm_context *ctx)
{
	uintptr_t *pml4 = (uintptr_t *)(ctx->arch.pml4_phys + PHYS_MAP_START);
	for(int i=0;i<256;i++)
		pml4[i] = 0;
}

void arch_mm_context_destroy(struct vm_context *ctx)
{
	mm_physical_deallocate(ctx->arch.pml4_phys);
}

void arch_mm_init(struct vm_context *ctx)
{
	ctx->arch.pml4_phys = ((uintptr_t)&initial_pml4 - KERNEL_VIRT_BASE) + KERNEL_PHYS_BASE;
	arch_mm_context_switch(ctx);
}

void arch_mm_context_switch(struct vm_context *ctx)
{
	asm volatile("mov %0, %%cr3" :: "r"(ctx->arch.pml4_phys) : "memory");
}

