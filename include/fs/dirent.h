#pragma once

#include <slab.h>
#include <fs/inode.h>

struct dirent {
	struct kobj_header _header;

	struct inode_id ino;
	size_t namelen;
	char name[256];
};

struct kobj kobj_dirent;
struct inode *dirent_get_inode(struct dirent *);

struct gd_dirent {
	int64_t        d_ino;    /* 64-bit inode number */
	int64_t        d_off;    /* 64-bit offset to next structure */
	unsigned short d_reclen; /* Size of this dirent */
	unsigned char  d_type;   /* File type */
	char           d_name[]; /* Filename (null-terminated) */
};


