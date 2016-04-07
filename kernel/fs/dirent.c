#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>

static void _dirent_put(void *obj)
{
	struct dirent *dir = obj;
	if(dir->inode) {
		inode_put(dir->inode);
	}
}

static void _dirent_init(void *obj)
{
	struct dirent *dir = obj;
	dir->inode = NULL;
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

