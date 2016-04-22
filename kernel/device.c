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

struct device *dev_get(int type, int major)
{
	if(S_ISCHR(type))
		return hash_lookup(&chars, &major, sizeof(int));
	else if(S_ISBLK(type))
		return hash_lookup(&blocks, &major, sizeof(int));
	assert(0);
}

struct file_calls *dev_get_fops(struct inode *node)
{
	struct device *dev = dev_get(node->mode, node->major);
	if(dev == NULL)
		return NULL;
	return dev->calls;
}

int dev_register(struct device *dev, struct file_calls *calls, int type)
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

void dev_attach(struct device *dev, struct attachment *at, int id, void *obj)
{
	at->id = id;
	at->obj = obj;
	hash_insert(&dev->attached, &at->id, sizeof(int), &at->elem, at);
}

void *dev_get_attached(struct device *dev, int id)
{
	struct attachment *at = hash_lookup(&dev->attached, &id, sizeof(int));
	return at ? at->obj : NULL;
}

