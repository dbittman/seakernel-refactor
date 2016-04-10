#pragma once

#include <lib/hash.h>
#include <slab.h>

#define MMAP_MAP_ANON   1
#define MMAP_MAP_MAPPED 2

#define PROT_WRITE 1
#define PROT_EXEC  2

struct inode;
struct inodepage;
struct mapping {
	struct kobj_header _header;
	struct hashelem elem;
	uintptr_t vpage;
	struct inode *node;
	union {
		struct inodepage *page;
		uintptr_t frame;
	};
	int prot, flags, nodepage;
};

struct process;
bool mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct inode *node, int nodepage);

int mmu_mappings_handle_fault(uintptr_t addr, int flags);
