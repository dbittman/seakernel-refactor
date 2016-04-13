#pragma once
#include <slab.h>
#include <lib/hash.h>
#include <mutex.h>
struct blockpoint;
struct inode_id {
	uint64_t fsid;
	uint64_t inoid;
};

struct inode;
#define INODEPAGE_DIRTY 1
struct inodepage {
	struct kobj_header _header;
	int page;
	uintptr_t frame;
	struct inode *node;
	_Atomic int flags;
};

#define INODE_FLAG_DIRTY 1
struct inode {
	struct kobj_header _header;
	_Atomic int flags;

	struct inode_id id;

	struct filesystem *fs;

	_Atomic uint16_t mode;
	_Atomic uint16_t links;
	_Atomic size_t length;

	_Atomic uint64_t atime, mtime, ctime;

	struct kobj_lru pages;

	int major, minor;
};

struct file;

void inode_put(struct inode *inode);
void inode_release_page(struct inode *node, struct inodepage *page);
uintptr_t inode_acquire_page(struct inode *node, int nodepage);
struct inodepage *inode_get_page(struct inode *node, int nodepage);
struct inode *inode_lookup(struct inode_id *id);
ssize_t inode_write_data(struct file *, size_t off, size_t len, const char *buf);
ssize_t inode_read_data(struct file *, size_t off, size_t len, char *buf);

static inline void inode_mark_dirty(struct inode *node)
{
	node->flags |= INODE_FLAG_DIRTY;
}


