#pragma once

#include <slab.h>

struct inode;
struct dirent {
	struct kobj_header _header;

	struct inode *inode;
};

