#include <device.h>
#include <file.h>
#include <system.h>
#include <block.h>

static struct file_calls block_ops = {
	.open = 0, .close = 0, .create = 0, .destroy = 0,
	.select = 0, .ioctl = 0, .map = 0, .unmap  =0,
	.read = 0, .write = 0,
};

void blockdev_register(struct blockdriver *driver)
{
	dev_register(&driver->device, &block_ops, S_IFBLK);
}

