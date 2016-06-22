#include <system.h>
#include <mmu.h>
#include <process.h>
#include <file.h>
#include <errno.h>
#include <device.h>
#include <worker.h>
#include <fs/sys.h>
#include <sys.h>
static struct device dev;

#define RX_RINGS 32
#define TX_RINGS 32

struct lodata {
	size_t rx_rings, tx_rings;
	void **rx, **tx;
	struct spinlock lock;
	void *ctl;
	size_t rxpos, rxpos_last, txpos, txpos_last;
};

static struct lodata lodata;

#define LO_CMD_TXSYNC 1
#define LO_CMD_RXSYNC 2

void _lo_worker_main(struct worker *worker)
{
	while(worker_notjoining(worker)) {
		while(lodata.txpos_last > lodata.txpos) {
			spinlock_acquire(&lodata.lock);
			if(lodata.rx[lodata.txpos] == NULL)
				lodata.rx[lodata.txpos] = (void *)mm_physical_allocate(arch_mm_page_size(0), false);
			memcpy(lodata.rx[lodata.txpos], lodata.tx[lodata.txpos], arch_mm_page_size(0));
			spinlock_release(&lodata.lock);
			lodata.txpos++;
			lodata.rxpos++;
		}
		sys_futex((void *)((uintptr_t)lodata.ctl - PHYS_MAP_START), FUTEX_WAKE, 1, NULL, NULL, 0);
	}
	worker_exit(worker, 0);
}

int _lo_ioctl(struct file *file, long cmd, long arg)
{
	(void)file;
	size_t *pos = (size_t *)arg;
	switch(cmd) {
		case LO_CMD_TXSYNC:
			lodata.txpos_last = *pos;
			break;
		case LO_CMD_RXSYNC:
			lodata.rxpos_last = *pos;
			*pos = lodata.rxpos;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

uintptr_t _lo_map(struct file *file, struct map_region *map, ptrdiff_t offset)
{
	(void)file;
	size_t ring = offset / arch_mm_page_size(0);
	if(map->nodepage == 2)
		return (uintptr_t)lodata.ctl;
	/* TODO: better test for tx or rx */
	void **slot = (map->nodepage == 0 ? &lodata.rx[ring] : &lodata.tx[ring]);
	spinlock_acquire(&lodata.lock);
	if(*slot == NULL) {
		*slot = (void *)mm_physical_allocate(arch_mm_page_size(0), true);
	}
	spinlock_release(&lodata.lock);
	return (uintptr_t)*slot;
}

void _lo_unmap(struct file *file, struct map_region *map, ptrdiff_t offset, uintptr_t phys)
{
	(void)file;
	(void)map;
	(void)offset;
	(void)phys;
}

struct worker _lo_worker;
static void _late_init(void)
{
	worker_start(&_lo_worker, _lo_worker_main, &lodata);
	int ret = sys_mknod("/dev/lo", S_IFCHR | 0666, makedev(dev.devnr, 0));
	assert(ret == 0);
}

static struct file_calls lo_calls = {
	.read = 0,
	.write = 0,

	.create = 0, .destroy = 0, .ioctl = _lo_ioctl, .select = 0, .open = 0, .close = 0,
	.map = _lo_map, .unmap = _lo_unmap,
};

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER))
static void _lo_init(void)
{
	init_register_late_call(&_late_init, NULL);
	dev_register(&dev, &lo_calls, S_IFCHR);
	memset(&lodata, 0, sizeof(lodata));
	spinlock_create(&lodata.lock);
	lodata.tx = (void *)mm_virtual_allocate(arch_mm_page_size(0), true);
	lodata.rx = (void *)mm_virtual_allocate(arch_mm_page_size(0), true);
	lodata.ctl = (void *)mm_physical_allocate(arch_mm_page_size(0), true);
}
