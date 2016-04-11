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

