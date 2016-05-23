#pragma once
#include <slab.h>
#include <lib/hash.h>
#include <mutex.h>
#include <fs/stat.h>
struct blockpoint;
struct inode_id {
	uint64_t fsid;
	uint64_t inoid;
};

struct inode;
#define INODEPAGE_DIRTY 1

struct inodepage_id {
	uint64_t fsid;
	uint64_t inoid;
	uint64_t page;
};

struct inodepage {
	struct kobj_header _header;
	struct inodepage_id id;
	uintptr_t frame;
	_Atomic int flags;
};

#define INODE_FLAG_DIRTY 1
struct inode {
	struct kobj_header _header;
	_Atomic int flags;
	struct mutex lock;

	struct inode_id id;

	struct filesystem *fs;
	struct filesystem * _Atomic mount;

	_Atomic uint16_t mode;
	_Atomic uint16_t links;
	_Atomic size_t length;

	_Atomic uint64_t atime, mtime, ctime;
	_Atomic int uid, gid;

	int major, minor;
};

extern struct kobj kobj_inode_page;
struct file;

void inode_put(struct inode *inode);
void inode_release_page(struct inodepage *page);
uintptr_t inode_acquire_page(struct inode *node, int nodepage);
struct inodepage *inode_get_page(struct inode *node, int nodepage);
struct inode *inode_lookup(struct inode_id *id);
ssize_t inode_write_data(struct file *, size_t off, size_t len, const char *buf);
ssize_t inode_read_data(struct file *, size_t off, size_t len, char *buf);
bool inode_check_perm(struct inode *node, int type);
bool inode_check_access(struct inode *node, int type);
ssize_t inode_do_write_data(struct inode *ino, size_t off, size_t len, const char *buf);
ssize_t inode_do_read_data(struct inode *ino, size_t off, size_t len, char *buf);

static inline void inode_mark_dirty(struct inode *node)
{
	node->flags |= INODE_FLAG_DIRTY;
}


