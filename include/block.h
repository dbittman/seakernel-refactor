#pragma once

struct blockdev {
	struct blockdriver *drv;
	int devid;
};

struct blockdriver {
	int blksz;
	int (*read_blocks)(struct blockdev *bd, long start, int count, void *buf);
	int (*write_blocks)(struct blockdev *bd, long start, int count, const void *buf);
};

