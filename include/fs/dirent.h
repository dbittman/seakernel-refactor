#pragma once

#include <slab.h>
#include <fs/inode.h>

#define DIRENT_UNLINK 1
#define DIRENT_UNCACHED 2

struct dirent {
	struct kobj_header _header;

	_Atomic int flags;
	struct inode_id ino;
	struct inode *pnode;
	struct inode_id parent;
	size_t namelen;
	char name[256];
};

#define DIRENT_ID_LEN (sizeof(char) * 256 + sizeof(size_t) + sizeof(struct inode_id))

struct kobj kobj_dirent;
struct inode *dirent_get_inode(struct dirent *);
void dirent_put(struct dirent *dir);
struct dirent *dirent_lookup(struct inode *node, const char *name, size_t namelen);
struct dirent *dirent_lookup_cached(struct inode *node, const char *name, size_t namelen);

struct gd_dirent {
	int64_t        d_ino;    /* 64-bit inode number */
	int64_t        d_off;    /* 64-bit offset to next structure */
	unsigned short d_reclen; /* Size of this dirent */
	unsigned char  d_type;   /* File type */
	char           d_name[]; /* Filename (null-terminated) */
};


