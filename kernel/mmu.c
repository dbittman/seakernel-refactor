#include <mmu.h>
#include <machine/machine.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include <printk.h>
#include <process.h>
#include <thread.h>
struct vm_context kernel_context;

static bool early_mm = true;
/* we don't want people to call these directly */
uintptr_t pmm_buddy_allocate(size_t length);
void pmm_buddy_deallocate(uintptr_t address);

extern int kernel_end;
static uintptr_t pm_start;
static uintptr_t pmm_early_alloc(size_t size)
{
	size = ((size - 1) & ~(arch_mm_page_size(0)-1)) + arch_mm_page_size(0);
	uintptr_t ret = pm_start;
	pm_start += size;
	return ret;
}

uintptr_t mm_physical_allocate(size_t length, bool clear)
{
	uintptr_t ret;
	if(early_mm) {
		ret = pmm_early_alloc(length);
	} else {
		ret = pmm_buddy_allocate(length);
	}
	if(clear)
		memset((void *)(ret + PHYS_MAP_START), 0, length);
	return ret;
}

void mm_physical_deallocate(uintptr_t address)
{
	if(!early_mm)
		pmm_buddy_deallocate(address);
}

static uintptr_t restore_page_phys;
#include <system.h>
extern int signal_restore_code_start;
extern int signal_restore_code_end;
__initializer static void _init_restore_page(void)
{
	restore_page_phys = mm_physical_allocate(arch_mm_page_size(0), true);
	memcpy((void *)(restore_page_phys + PHYS_MAP_START), &signal_restore_code_start,
			(uintptr_t)&signal_restore_code_end - (uintptr_t)&signal_restore_code_start);
}

void mm_fault_entry(uintptr_t address, int flags, uintptr_t from)
{
	if((address & page_mask(0)) == SIGNAL_RESTORE_PAGE && (flags & FAULT_USER) && (flags & FAULT_ERROR_PRES)) {
		arch_mm_virtual_map(current_thread->process->ctx, SIGNAL_RESTORE_PAGE, restore_page_phys, arch_mm_page_size(0), MAP_EXECUTE | MAP_USER);
		return;
	}
	if(current_thread->process && address >= USER_REGION_START && address < USER_REGION_END) {
		if(mmu_mappings_handle_fault(address, flags))
			return;
		printk("SIGSEGV - process %d, addr %lx cause %x (tls %lx) from %lx\n", current_thread->process->pid, address, flags, current_thread->arch.fs, from);
		thread_send_signal(current_thread, SIGSEGV);
		return;
	}
	panic(0, "PF [t%ld, p%d] - %lx %x, from %lx", current_thread->tid, current_thread->process->pid, address, flags, from);
}

void mm_early_init(void)
{
	pm_start = (((uintptr_t)&kernel_end - 1) & ~(arch_mm_page_size(0) - 1)) + arch_mm_page_size(0);
	pm_start -= (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE);
}

/* TODO (minor) [dbittman]: make sure to map certain areas as PRIVATE when we need to! */
void mm_init(void)
{
	size_t memlen = machine_get_memlen();
	arch_mm_init(&kernel_context);
	printk("Remapping physical page mapping...");
	uintptr_t addr = PHYS_MAP_START + PHYS_MEMORY_START;
	uintptr_t phys = addr - PHYS_MAP_START;
	size_t count = 0;
	while(count < memlen || phys < (MMIO_PHYS_START + MMIO_PHYS_LENGTH)) {
 		/* TODO (minor) [dbittman]: need to not use MAP_USER here. */
		int flags = MAP_WRITE | MAP_ACCESSED | MAP_EXECUTE | MAP_USER;
		if(phys >= MMIO_PHYS_START && phys < (MMIO_PHYS_START + MMIO_PHYS_LENGTH)) {
			flags |= MAP_DEVICE;
		}
		if(!arch_mm_virtual_map(&kernel_context, addr, phys, arch_mm_page_size(1), flags)) {
			bool r = arch_mm_virtual_chattr(&kernel_context, addr, flags);
			assert(r);
		}
		addr += arch_mm_page_size(1);
		phys += arch_mm_page_size(1);
		if(!(flags & MAP_DEVICE))
			count += arch_mm_page_size(1);
	}
	printk(" ok\nInitializing buddy allocator... (%lu MB)", (KERNEL_PHYS_BASE + memlen - pm_start) / (1024 * 1024));
	pmm_buddy_init();
	early_mm = false;
	
	phys = PHYS_MEMORY_START;
	count = 0;
	while(count < memlen) {
		if(!(phys >= MMIO_PHYS_START && phys < (MMIO_PHYS_START + MMIO_PHYS_LENGTH))) {
			if(phys >= pm_start) {
				mm_physical_deallocate(phys);
			}
			count += MM_BUDDY_MIN_SIZE;
		}
		phys += MM_BUDDY_MIN_SIZE;
	}
	printk(" ok\n");
}

static void _vm_context_create(void *obj)
{
	struct vm_context *ctx = obj;
	arch_mm_context_create(ctx);
}

static void _vm_context_init(void *obj)
{
	struct vm_context *ctx = obj;
	arch_mm_context_init(ctx);
}

static void _vm_context_destroy(void *obj)
{
	struct vm_context *ctx = obj;
	arch_mm_context_destroy(ctx);
}

struct kobj kobj_vm_context = {
	.name = "vm_context",
	.size = sizeof(struct vm_context),
	.create = _vm_context_create,
	.put = 0,
	.init = _vm_context_init,
	.destroy = _vm_context_destroy,
	.initialized = false
};

static void _print_range(struct vm_context *ctx, uintptr_t start, uintptr_t end)
{
	uintptr_t addr = start;
	uintptr_t run_start = 0;
	int run_flags = 0;
	while(addr != end) {
		uintptr_t phys;
		int flags;
		if(arch_mm_virtual_getmap(ctx, addr, &phys, &flags)) {
			if(run_flags == 0) {
				run_start = addr;
				run_flags = flags;
			} else if(run_flags != flags) {
				printk("%lx - %lx %x\n", run_start, addr - 1, run_flags);
				run_start = addr;
				run_flags = flags;
			}
		} else {
			if(run_flags) {
				printk("%lx - %lx %x\n", run_start, addr - 1, run_flags);
			}
			run_start = 0;
			run_flags = 0;
		}
		addr += arch_mm_page_size(1);
	}
	if(run_flags) {
		printk("%lx - %lx %x\n", run_start, addr - 1, run_flags);
	}
}

void mm_print_context(struct vm_context *ctx)
{
	_print_range(ctx, 0, (1ul << 48));
	_print_range(ctx, 0xFFFF000000000000, 0);
}

