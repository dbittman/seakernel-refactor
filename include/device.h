#pragma once

#include <lib/hash.h>

struct inode;
struct inode_calls;

struct device {
	struct hashelem elem;
	struct inode_calls *calls;
	int devnr;
	struct hash attached;
};

struct inode_calls *dev_get_iops(struct inode *);

