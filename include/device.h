#pragma once

#include <lib/hash.h>

struct inode;
struct file_calls;

struct device {
	struct hashelem elem;
	struct file_calls *calls;
	int devnr;
	struct hash attached;
};

#define DEVICE_INITIALIZER_ORDER 100

struct file_calls *dev_get_fops(struct inode *);
int dev_register(struct device *dev, struct file_calls *calls, int type);

#define major(x) \
	        ((unsigned)( (((x)>>31>>1) & 0xfffff000) | (((x)>>8) & 0x00000fff) ))
#define minor(x) \
	        ((unsigned)( (((x)>>12) & 0xffffff00) | ((x) & 0x000000ff) ))

#define makedev(x,y) ( \
		        ((((unsigned long)x)&0xfffff000ULL) << 32) | \
		        ((((unsigned long)x)&0x00000fffULL) << 8) | \
		        (((y)&0xffffff00ULL) << 12) | \
		        (((y)&0x000000ffULL)) )

