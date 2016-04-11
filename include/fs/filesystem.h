#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <slab.h>
#include <fs/inode.h>

struct inode;
struct dirent;

struct inode_ops {
	int (*read_page)(struct inode *, int pagenr, uintptr_t phys);
	int (*write_page)(struct inode *, int pagenr, uintptr_t phys);
	int (*sync)(struct inode *);
	int (*update)(struct inode *);
	int (*lookup)(struct inode *, const char *name, size_t namelen, struct dirent *);
	int (*link)(struct inode *, const char *name, size_t namelen, struct inode *);
};

struct filesystem;
struct fs_ops {
	int (*load_inode)(struct filesystem *fs, uint64_t inoid, struct inode *node);
	int (*alloc_inode)(struct filesystem *fs, uint64_t *inoid);
};

struct fsdriver {
	struct inode_ops *inode_ops;
	struct fs_ops *fs_ops;
	const char *name;
	uint64_t rootid;
	struct hashelem elem;
};

struct filesystem {
	struct kobj _header;
	uint64_t id;
	struct fsdriver *driver;
	void *fsdata;
};

#define FILESYSTEM_INIT_ORDER 100

int fs_load_inode(uint64_t fsid, uint64_t inoid, struct inode *node);

extern struct fsdriver ramfs;

struct kobj kobj_filesystem;

static inline struct inode *fs_inode_lookup(struct filesystem *fs, uint64_t inoid)
{
	struct inode_id id = {.fsid = fs->id, .inoid = inoid };
	return inode_lookup(&id);
}

#include <mmu.h>
static inline int fs_max_page(size_t len)
{
	return len > 0 ? (len - 1) / arch_mm_page_size(0) : -1;
}

