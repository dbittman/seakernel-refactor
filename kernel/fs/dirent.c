#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>
#include <string.h>
#include <fs/filesystem.h>

/* TODO: dirent cache? */

/*
 * Dirents are keyed with parent ID, name length, and name. When looked up, they
 * are returned from the LRU cache. Unlink operation sets a flag and removes them
 * from the LRU cache. When dirent.put is called, if the flag is set it decrements
 * the inode link count.
 *
 * Note that we remove the dirent from the LRU because that way no other references
 * to it can be taken after it is unlinked. Also, this way when all references to it
 * are dropped, it will actually decrement the link count on the inode as well.
 */


static void _dirent_init(void *obj)
{
	struct dirent *dir = obj;
	memset(dir->name, 0, sizeof(dir->name));
}

struct kobj kobj_dirent = {
	KOBJ_DEFAULT_ELEM(dirent),
	.init = _dirent_init,
	.create = NULL, .destroy = NULL, .put = NULL,
};

struct kobj_lru dirent_lru;

static bool _dirent_initialize(void *obj, void *id, void *data)
{
	(void)data;
	struct dirent *dir = obj;
	dir->flags = 0;
	memcpy(&dir->parent, id, DIRENT_ID_LEN);
	struct inode *parent = inode_lookup(&dir->parent);

	mutex_acquire(&parent->lock);
	int res = parent->fs->driver->inode_ops->lookup(parent, dir->name, dir->namelen, dir);
	mutex_release(&parent->lock);
	if(res != 0) {
		kobj_lru_mark_error(&dirent_lru, obj, &dir->parent);
		return false;
	}
	kobj_lru_mark_ready(&dirent_lru, obj, &dir->parent);
	return true;
}

static void _dirent_release(void *obj, void *data)
{
	(void)data;

}

static void _dirent_drop(void *obj, void *data)
{

}

__initializer static void _init_dirent_cache(void)
{
	kobj_lru_create(&dirent_lru, DIRENT_ID_LEN, 0, &kobj_dirent, _dirent_initialize, _dirent_release, _dirent_drop, NULL);
}

struct inode *dirent_get_inode(struct dirent *dir)
{
	struct inode *node = inode_lookup(&dir->ino);
	if(node->mount) {
		struct inode_id id = {.fsid = node->mount->id, .inoid = node->mount->driver->rootid};
		inode_put(node);
		node = inode_lookup(&id);
	}
	if(!strncmp(dir->name, "..", 2) && dir->namelen == 2 && node->id.inoid == node->fs->driver->rootid) {
		struct inode *up = inode_lookup(&node->fs->up_mount);
		inode_put(node);
		node = up;
	}
	return node;
}

