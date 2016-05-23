#include <lib/hash.h>
#include <stddef.h>
#include <stdint.h>
#include <mmu.h>
#include <system.h>
#include <machine/machine.h>
#include <printk.h>
#include <frame.h>
static struct frame *frames;

__orderedinitializer(0) static void frames_init(void)
{
	size_t memlen = machine_get_memlen();
	size_t numframes = (memlen - PHYS_MEMORY_START) / arch_mm_page_size(0);
	printk("[kernel]: Allocating %llu MB for page frames\n", __round_up_pow2(numframes * sizeof(struct frame)) / (1024 * 1024));
	frames = (void *)mm_virtual_allocate( __round_up_pow2(numframes * sizeof(struct frame)), true);
}

struct frame *frame_get_from_address(uintptr_t phys)
{
	return &frames[phys / arch_mm_page_size(0)];
}

void frame_acquire(uintptr_t phys)
{
	frames[phys / arch_mm_page_size(0)].count++;
}

uintptr_t frame_allocate(int level, int flags)
{
	uintptr_t phys = mm_physical_allocate(arch_mm_page_size(level), true);
	struct frame *frame = frame_get_from_address(phys);
	assert(frame->count == 0);
	if(!(flags & FRAME_ZEROCOUNT))
		frame->count = 1;
	frame->flags = flags;
	return phys;
}

long frame_release(uintptr_t phys)
{
	struct frame *frame = &frames[phys / arch_mm_page_size(0)];
	assert(frame->count > 0);
	long c = --frame->count;
	if(c == 0 && !(frame->flags & FRAME_PERSIST))
		mm_physical_deallocate(phys);
	return c;
}

