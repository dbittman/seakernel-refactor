#include "ext2.h"
#include <block.h>
#include <system.h>
#include <fs/path.h>
#include <printk.h>
#include <fs/inode.h>

int ext2_read_blockdev(struct ext2 *fs, unsigned long block, int count, uintptr_t phys, bool cache)
{
	return block_read(fs->bdev, block, count, phys, cache);
}


static void _late_init(void)
{
	printk("TESTING\n");
	struct inode *node;
	int err = fs_path_resolve("/dev/ada0", 0, 0, 0, 0, &node);
	if(err < 0) {
		panic(0, "no find");
	}
	struct blockdev *bd = blockdev_get(node->major, node->minor);
	assert(bd != NULL);
	uintptr_t p = mm_physical_allocate(0x1000, false);
	memset((void *)(p + PHYS_MAP_START), 1, 0x1000);

	int r = block_read(bd, 0, 1, p, false);
	printk("r: %d\n", r);
	for(int i=0;i<512;i++) {
		printk("%x ", *(unsigned char *)(p + PHYS_MAP_START + i));
	}
}

__initializer static void ext2_init(void)
{
	init_register_late_call(&_late_init, NULL);
}


