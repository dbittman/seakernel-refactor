#pragma once
#include <device.h>

struct blockdev {
	struct blockdriver *drv;
	int devid;
	void *devdata;
};

struct blockdriver {
	const char *name;
	int blksz;
	int devnr;
	int (*read_blocks)(struct blockdev *bd, unsigned long start, int count, uintptr_t phys);
	int (*write_blocks)(struct blockdev *bd, unsigned long start, int count, uintptr_t phys);
	struct device device;
};

void blockdev_register(struct blockdriver *driver);
