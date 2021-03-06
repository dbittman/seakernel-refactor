#include <device.h>
#include <file.h>
#include <system.h>
#include <block.h>
#include <string.h>
#include <mmu.h>
#include <thread.h>
#include <printk.h>

static struct file_calls block_ops = {
	.open = 0, .close = 0, .create = 0, .destroy = 0,
	.poll = 0, .ioctl = 0, .map = 0, .unmap  =0,
	.read = 0, .write = 0,
};

static void _req_init(void *obj)
{
	struct request *req = obj;
	req->ret_count = 0;
	assert(req->wait.waitlist.count == 0);
}

static void _req_create(void *obj)
{
	struct request *req = obj;
	blocklist_create(&req->wait);
	_req_init(obj);
}

static void _req_put(void *obj)
{
	struct request *req = obj;
	kobj_putref(req->bd);
}

static struct kobj kobj_request = {
	KOBJ_DEFAULT_ELEM(request),
	.init = _req_init,
	.create = _req_create,
	.put = _req_put, .destroy = NULL,
};

static void block_cache_write(struct blockdev *bd, unsigned long block, uintptr_t phys)
{
	spinlock_acquire(&bd->cache_lock);
	
	struct block *bl = hash_lookup(&bd->cache, &block, sizeof(block));
	if(!bl) {
		bl = kobj_allocate(&bd->drv->kobj_block);
		bl->block = block;
		hash_insert(&bd->cache, &bl->block, sizeof(bl->block), &bl->elem, bl);
	}

	memcpy(bl->data, (void *)(phys + PHYS_MAP_START), bd->drv->blksz);
	bl->dirty = true;

	spinlock_release(&bd->cache_lock);
}

static bool block_cache_read(struct blockdev *bd, unsigned long block, uintptr_t phys)
{
	spinlock_acquire(&bd->cache_lock);
	struct block *bl = hash_lookup(&bd->cache, &block, sizeof(block));
	if(!bl) {
		spinlock_release(&bd->cache_lock);
		return false;
	}

	memcpy((void *)(phys + PHYS_MAP_START), bl->data, bd->drv->blksz);
	spinlock_release(&bd->cache_lock);
	return true;
}

#if 0
void _handle_request(void *_req)
{
	struct request *req = _req;
	req->bd->drv->handle_req(req->bd, req);
	kobj_putref(req);
}
#endif

static void __elevator(struct worker *worker)
{
	struct blockdev *bd = worker_arg(worker);
	struct blockpoint bp;
	while(worker_notjoining(worker)) {
		workqueue_execute(&bd->requests);
		if(workqueue_empty(&bd->requests)) {
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&bd->wait, &bp);
			if(workqueue_empty(&bd->requests)) {
				schedule();
			}
			blockpoint_cleanup(&bp);
		}
	}
	worker_exit(worker, 0);
}

static void _blockdev_init(void *obj)
{
	struct blockdev *bd = obj;
	worker_start(&bd->kio, __elevator, bd);
}

static void _blockdev_put(void *obj)
{
	struct blockdev *bd = obj;
	while(!worker_join(&bd->kio)) schedule();
}

static void _blockdev_create(void *obj)
{
	struct blockdev *bd = obj;
	workqueue_create(&bd->requests);
	hash_create(&bd->cache, HASH_LOCKLESS, 4096);
	spinlock_create(&bd->cache_lock);
	blocklist_create(&bd->wait);
	_blockdev_init(obj);
}

struct kobj kobj_blockdev = {
	KOBJ_DEFAULT_ELEM(blockdev),
	.init = _blockdev_init,
	.put = _blockdev_put,
	.destroy = NULL, .create = _blockdev_create,
};

void blockdriver_register(struct blockdriver *driver)
{
	driver->_next_id = 0;
	dev_register(&driver->device, &block_ops, S_IFBLK);
}

static int __do_request(struct blockdev *bd, unsigned long start, int count, uintptr_t phys)
{
	struct request *req = kobj_allocate(&kobj_request);
	req->bd = kobj_getref(bd);
	req->start = start;
	req->phys = phys;
	req->count = count;
	req->write = false;
	req->ret_count = -1;
	req->bd->drv->handle_req(req->bd, req);

	struct blockpoint bp;
	blockpoint_create(&bp, BLOCK_UNINTERRUPT, 0);
	blockpoint_startblock(&req->wait, &bp);

	//struct workitem wi = { .fn = _handle_request, .arg = kobj_getref(req) };
	//workqueue_insert(&bd->requests, &wi);
	//blocklist_unblock_all(&bd->wait);
	if(req->ret_count == -1) {
		req->bd->drv->handle_req(req->bd, req);
		schedule();
	}

	enum block_result res = blockpoint_cleanup(&bp);
	assert(res == BLOCK_RESULT_BLOCKED || res == BLOCK_RESULT_UNBLOCKED);

	count = req->ret_count;
	if(count == -1) {
		panic(0, "failed to wait for request: %d", count);
	}
	assert(count != -1);

	kobj_putref(req);
	return count;
}

int block_read(struct blockdev *bd, unsigned long start, int count, uintptr_t phys, bool cache)
{
	for(int i=0;i<count;i++) {
		if(!block_cache_read(bd, start + i, phys + i * bd->drv->blksz)) {
			/* TODO: multiple block points, waiting on multiple blocks? */
			if(__do_request(bd, start + i, 1, phys + i * bd->drv->blksz) == 0)
				return i;
			if(cache) {
				block_cache_write(bd, start + i, phys + bd->drv->blksz * i);
			}
		}
	}
	return count;
}

int block_write(struct blockdev *bd, unsigned long start, int count, uintptr_t phys)
{
	for(int i=0;i<count;i++) {
		block_cache_write(bd, start + i, phys + bd->drv->blksz * i);
	}
	return count;
}

struct blockdev *blockdev_get(int major, int minor)
{
	struct device *dev = dev_get(S_IFBLK, major);
	struct blockdev *bd = dev ? dev_get_attached(dev, minor) : NULL;
	return bd ? kobj_getref(bd) : NULL;
}

void blockdev_register(struct blockdev *bd, struct blockdriver *drv)
{
	bd->drv = drv;
	bd->devid = blockdev_getid(drv);
	dev_attach(&drv->device, &bd->attach, bd->devid, kobj_getref(bd));
}

