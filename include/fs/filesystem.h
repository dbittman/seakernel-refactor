#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <slab.h>

struct inode;

struct inode_ops {
	bool (*read_page)(struct inode *, int pagenr, uintptr_t phys);
	bool (*write_page)(struct inode *, int pagenr, uintptr_t phys);
	bool (*sync)(struct inode *);
	bool (*update)(struct inode *);
};

struct filesystem;
struct fs_ops {
	bool (*load_inode)(struct filesystem *fs, uint64_t inoid, struct inode *node);
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

bool fs_load_inode(uint64_t fsid, uint64_t inoid, struct inode *node);

extern struct fsdriver ramfs;

struct kobj kobj_filesystem;
