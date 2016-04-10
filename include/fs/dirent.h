#pragma once

#include <slab.h>
#include <fs/inode.h>

struct dirent {
	struct kobj_header _header;

	struct inode_id ino;
	char name[256];
};

struct inode *dirent_get_inode(struct dirent *);
