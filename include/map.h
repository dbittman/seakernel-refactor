#pragma once

#include <lib/hash.h>
#include <slab.h>

#define MMAP_MAP_SHARED   0x1
#define MMAP_MAP_PRIVATE  0x2
#define MMAP_MAP_FIXED   0x10
#define MMAP_MAP_ANON    0x20

#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED 2

struct inode;
struct inodepage;
struct mapping {
	struct kobj_header _header;
	uintptr_t vpage;
	struct file *file;
	struct inodepage *page;
	int prot, flags, nodepage;
};

struct map_region {
	struct kobj_header _header;
	struct linkedentry entry;
	uintptr_t start;
	size_t psize;
	size_t length;
	
	struct file *file;
	int nodepage;
	int prot;
	int flags;
};

struct process;


void map_region_setup(struct process *proc, uintptr_t start, size_t len, int prot, int flags, struct file *file, int nodepage, size_t psize, bool locked);
void map_mmap(struct process *proc, uintptr_t virt, size_t len, int prot, int flags, struct file *file, size_t off);

int mmu_mappings_handle_fault(struct process *proc, uintptr_t addr, int flags);
int mapping_resize(struct process *proc, uintptr_t virt, size_t oldsz, size_t newsz);
int mapping_move(struct process *proc, uintptr_t virt, size_t oldsz, size_t newsz, uintptr_t new);
int map_change_protect(struct process *proc, uintptr_t virt, size_t len, int prot);
void map_region_remove(struct process *proc, uintptr_t start, size_t len, bool locked);

