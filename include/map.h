#pragma once

#include <lib/hash.h>
#include <slab.h>

#define MAP_ANON   1
#define MAP_MAPPED 2

struct mapping {
	struct kobj_header _header;
	struct hashelem elem;
	uintptr_t vpage;
	union {
		struct inode *node;
		uintptr_t frame;
	};
	int prot, flags, nodepage;
};

