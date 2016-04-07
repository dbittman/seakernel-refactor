#pragma once
#include <slab.h>
#include <lib/hash.h>
#define FOT_PRESENT 1

struct fot_entry {
	uint64_t guid[2];
	uint64_t res;
	uint64_t options;
} __attribute__((packed));

struct fot {
	uint32_t length;
	uint32_t flags;
	struct fot_entry entries[];
} __attribute__((packed));

struct object {
	struct kobj_header _header;
	uint64_t guid[2];
	size_t length;
	struct hashelem elem;

	struct hash physicals;
	struct fot *fot;
};

void frame_release(uintptr_t phys);
uintptr_t frame_allocate(void);
void frame_acquire(uintptr_t phys);

extern struct kobj kobj_object;
uintptr_t object_allocate_frame(struct object *obj, int pagenr);
void object_assign_guid(struct object *obj, uint64_t *guid);
struct object *object_get_by_guid(uint64_t *guid);

