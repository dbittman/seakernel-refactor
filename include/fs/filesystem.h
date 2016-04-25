#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <slab.h>
#include <fs/inode.h>
#include <mutex.h>
#include <fs/dirent.h>

struct inode;
struct dirent;
struct blockdev;

struct inode_ops {
	int (*read_page)(struct inode *, int pagenr, uintptr_t phys);
	int (*write_page)(struct inode *, int pagenr, uintptr_t phys);
	int (*sync)(struct inode *);
	int (*update)(struct inode *);
	int (*lookup)(struct inode *, const char *name, size_t namelen, struct dirent *);
	int (*link)(struct inode *, const char *name, size_t namelen, struct inode *);
	int (*unlink)(struct inode *, const char *name, size_t namelen);
	size_t (*getdents)(struct inode *node, _Atomic size_t *, struct gd_dirent *, size_t);
	int (*readlink)(struct inode *node, char *path, size_t);
	int (*writelink)(struct inode *node, const char *path);
};

struct filesystem;
struct fs_ops {
	int (*mount)(struct filesystem *fs, struct blockdev *bd, unsigned long flags);
	int (*unmount)(struct filesystem *fs);
	int (*load_inode)(struct filesystem *fs, uint64_t inoid, struct inode *node);
	int (*alloc_inode)(struct filesystem *fs, uint64_t *inoid);
	int (*update_inode)(struct filesystem *fs, struct inode *node);
	void (*release_inode)(struct filesystem *fs, struct inode *node);
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
	struct mutex lock;
	struct inode_id up_mount;
};

#define FILESYSTEM_INIT_ORDER 100

int fs_load_inode(uint64_t fsid, uint64_t inoid, struct inode *node);
void fs_update_inode(struct inode *node);
int filesystem_register(struct fsdriver *driver);
struct filesystem *fs_load_filesystem(struct blockdev *bd, const char *type, unsigned long flags, int *err);
void fs_unload_filesystem(struct filesystem *fs);
void filesystem_deregister(struct fsdriver *driver);
int fs_mount(struct inode *point, struct filesystem *fs);

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
	return len > 0 ? (int)((len - 1) / arch_mm_page_size(0)) : -1;
}

