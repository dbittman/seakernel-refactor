#pragma once

#include <lib/hash.h>
#include <slab.h>

#define MAP_ANON   1
#define MAP_MAPPED 2

#define PROT_WRITE 1
#define PROT_EXEC  2

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

struct process;
struct inode;
bool mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct inode *node, int nodepage);

int mmu_mappings_handle_fault(uintptr_t addr, int flags);
