#include "ext2.h"

int ext2_read_blockdev(struct ext2 *fs, unsigned long block, int count, uintptr_t phys, bool cache)
{
	block_read(fs->bdev, block, count, phys, cache);
	return fs->bdev->driver->read_blocks(fs->bdev, block, count, phys);
}

