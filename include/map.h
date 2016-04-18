#pragma once

#include <lib/hash.h>
#include <slab.h>

#define MMAP_MAP_SHARED   0x1
#define MMAP_MAP_PRIVATE  0x2
#define MMAP_MAP_FIXED   0x10
#define MMAP_MAP_ANON    0x20
#define MMAP_MAP_MAPPED 0x80000000

#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

struct inode;
struct inodepage;
struct mapping {
	struct kobj_header _header;
	struct hashelem elem;
	uintptr_t vpage;
	struct file *file;
	union {
		struct inodepage *page;
		uintptr_t frame;
	};
	int prot, flags, nodepage;
};

struct process;
struct mapping *mapping_establish(struct process *proc, uintptr_t virtual, int prot,
		int flags, struct file *, int nodepage);

int mmu_mappings_handle_fault(uintptr_t addr, int flags);
void map_mmap(uintptr_t virtual, struct file *, int prot, int flags, size_t len, size_t off);
void map_unmap(uintptr_t virtual, size_t length);

