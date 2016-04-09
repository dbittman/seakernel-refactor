#pragma once
#include <slab.h>
#include <lib/hash.h>
struct inode_id {
	uint64_t devid;
	uint64_t inoid;
};

struct inode;
struct inode_ops {
	bool (*read_page)(struct inode *, size_t pagenr, uintptr_t phys);
	bool (*write_page)(struct inode *, size_t pagenr, uintptr_t phys);
	bool (*sync)(struct inode *);
	bool (*update)(struct inode *);
};

struct inode {
	struct kobj_header _header;

	struct inode_id id;
	struct inode_ops *ops;

	uint16_t mode;
	uint16_t links;

	uint64_t atime, mtime, ctime;

	struct hash pages;
};

void inode_put(struct inode *inode);
void inode_release_page(struct inode *node, int nodepage);
uintptr_t inode_acquire_page(struct inode *node, int nodepage);
uintptr_t inode_get_page(struct inode *node, int nodepage);

