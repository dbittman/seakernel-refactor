#pragma once
#include <slab.h>
#include <lib/hash.h>
#include <mutex.h>
struct inode_id {
	uint64_t fsid;
	uint64_t inoid;
};

struct inode;
struct inodepage {
	int page;
	uintptr_t frame;
	struct mutex lock;
	struct inode *node;
};

struct inode {
	struct kobj_header _header;

	struct inode_id id;

	struct filesystem *fs;

	uint16_t mode;
	uint16_t links;

	uint64_t atime, mtime, ctime;

	struct kobj_lru pages;
};

void inode_put(struct inode *inode);
void inode_release_page(struct inode *node, struct inodepage *page);
uintptr_t inode_acquire_page(struct inode *node, int nodepage);
struct inodepage *inode_get_page(struct inode *node, int nodepage);
struct inode *inode_lookup(struct inode_id *id);
