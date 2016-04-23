#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>
#include <string.h>
#include <fs/filesystem.h>

struct kobj kobj_dirent = KOBJ_DEFAULT(dirent);

struct inode *dirent_get_inode(struct dirent *dir)
{
	struct inode *node = inode_lookup(&dir->ino);
	if(node->mount) {
		struct inode_id id = {.fsid = node->mount->id, .inoid = node->mount->driver->rootid};
		kobj_putref(node);
		node = inode_lookup(&id);
	}
	if(!strncmp(dir->name, "..", 2) && dir->namelen == 2 && node->id.inoid == node->fs->driver->rootid) {
		struct inode *up = inode_lookup(&node->fs->up_mount);
		kobj_putref(node);
		node = up;
	}
	return node;
}

