#pragma once

#include <slab.h>

struct dirent;
struct file {
	struct kobj_header _header;
	struct dirent *dirent;
};

extern struct kobj kobj_inode;
