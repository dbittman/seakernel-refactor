#include <mmu.h>
#include <system.h>
#include <machine/machine.h>
#include <printk.h>
#include <obj/object.h>
#include <string.h>
struct frame {
	struct hashelem elem;
	size_t framenr, pagenr;
	_Atomic int count;
};

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

uintptr_t frame_allocate(void)
{
	uintptr_t phys = mm_physical_allocate(arch_mm_page_size(0), true);
	frames[phys / arch_mm_page_size(0)].framenr = phys / arch_mm_page_size(0);
	frame_acquire(phys);
	return phys;
}

void frame_release(uintptr_t phys)
{
	if(--frames[phys / arch_mm_page_size(0)].count == 0)
		mm_physical_deallocate(phys);
}

static void _object_create(void *_obj)
{
	struct object *obj = _obj;
	hash_create(&obj->physicals, 0, 1024 / sizeof(struct linkedlist) /* TODO */);
}

static void _object_destroy(void *_obj)
{
	struct object *obj = _obj;
	hash_destroy(&obj->physicals);
}

struct kobj kobj_object = {
	.name = "object",
	.size = sizeof(struct object),
	.create = _object_create,
	.init = NULL,
	.put = NULL,
	.destroy = _object_destroy,
	.initialized = false
};

static struct hash objects;

__orderedinitializer(0) static void objects_init(void)
{
	hash_create(&objects, 0, 1024 / sizeof(struct linkedlist));
}

uintptr_t object_allocate_frame(struct object *obj, int pagenr)
{
	uintptr_t phys = frame_allocate();
	struct frame *frame = frame_get_from_address(phys);
	frame->pagenr = pagenr;
	hash_insert(&obj->physicals, &frame->pagenr, sizeof(frame->pagenr), &frame->elem, (void *)phys);
	return phys;
}

struct object *object_get_by_guid(uint64_t *guid)
{
	return hash_lookup(&objects, guid, 16);
}

void object_assign_guid(struct object *obj, uint64_t *guid)
{
	memcpy(obj->guid, guid, 16);
	hash_insert(&objects, obj->guid, 16, &obj->elem, obj);
}

