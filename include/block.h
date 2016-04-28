#pragma once
#include <device.h>
#include <lib/hash.h>
#include <workqueue.h>
#include <slab.h>
#include <worker.h>
struct blockdev {
	struct kobj_header _header;
	struct blockdriver *drv;
	int devid;
	void *devdata;
	struct workqueue requests;
	struct hash cache;
	struct spinlock cache_lock;
	struct worker kio;
	struct attachment attach;
	struct blocklist wait;
};

struct request {
	struct kobj_header _header;
	struct blockdev *bd;
	struct blocklist wait;
	unsigned long start;
	uintptr_t phys;
	_Atomic int count, ret_count;
	bool write;
};

struct blockdriver {
	const char *name;
	int blksz;
	int devnr;
	_Atomic int _next_id;
	int (*read_blocks)(struct blockdev *bd, unsigned long start, int count, uintptr_t phys);
	int (*write_blocks)(struct blockdev *bd, unsigned long start, int count, uintptr_t phys);
	int (*handle_req)(struct blockdev *bd, struct request *req);
	struct device device;
	struct kobj kobj_block;
};

struct block {
	struct kobj_header _header;
	unsigned long block;
	struct hashelem elem;
	bool dirty;
	char data[];
};

static inline int blockdev_getid(struct blockdriver *drv)
{
	return drv->_next_id++;
}

extern struct kobj kobj_blockdev;

void blockdriver_register(struct blockdriver *driver);
void blockdev_register(struct blockdev *bd, struct blockdriver *drv);
struct blockdev *blockdev_get(int major, int minor);
int block_read(struct blockdev *bd, unsigned long start, int count, uintptr_t phys, bool cache);
int block_write(struct blockdev *bd, unsigned long start, int count, uintptr_t phys);
