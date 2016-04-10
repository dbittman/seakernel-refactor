#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>

static void _dirent_put(void *obj)
{
	(void)obj;
}

static void _dirent_init(void *obj)
{
	(void)obj;
}

static void _dirent_create(void *obj)
{
	_dirent_init(obj);
}

struct kobj kobj_dirent = {
	.initialized = false,
	.name = "dirent",
	.size = sizeof(struct dirent),
	.init = _dirent_init,
	.create = _dirent_create,
	.put = _dirent_put,
	.destroy = NULL,
};

struct inode *dirent_get_inode(struct dirent *dir)
{
	return inode_lookup(&dir->ino);
}

