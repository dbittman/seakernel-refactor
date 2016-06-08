#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>
#include <string.h>
#include <fs/filesystem.h>

/* TODO: dirent cache? */

struct kobj kobj_dirent = KOBJ_DEFAULT(dirent);

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

