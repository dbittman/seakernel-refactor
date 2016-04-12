#include <device.h>
#include <system.h>
#include <fs/inode.h>
#include <fs/stat.h>
#include <assert.h>
struct hash blocks;
struct hash chars;

static _Atomic int _next_maj = 0;

__orderedinitializer(DEVICE_INITIALIZER_ORDER) static void _init_devs(void)
{
	hash_create(&blocks, 0, 32);
	hash_create(&chars, 0, 32);
}

static struct device *__get_device(struct inode *node)
{
	if(S_ISCHR(node->mode))
		return hash_lookup(&chars, &node->major, sizeof(int));
	else if(S_ISBLK(node->mode))
		return hash_lookup(&blocks, &node->major, sizeof(int));
	assert(0);
}

struct inode_calls *dev_get_iops(struct inode *node)
{
	struct device *dev = __get_device(node);
	if(dev == NULL)
		return NULL;
	return dev->calls;
}

int dev_register(struct device *dev, struct inode_calls *calls, int type)
{
	dev->devnr = ++_next_maj;
	dev->calls = calls;
	hash_create(&dev->attached, 0, 128);
	if(S_ISCHR(type))
		hash_insert(&chars, &dev->devnr, sizeof(int), &dev->elem, dev);
	else if(S_ISBLK(type))
		hash_insert(&blocks, &dev->devnr, sizeof(int), &dev->elem, dev);
	else
		assert(0);
	return dev->devnr;
}

