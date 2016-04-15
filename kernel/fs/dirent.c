#include <slab.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <system.h>

struct kobj kobj_dirent = KOBJ_DEFAULT(dirent);

struct inode *dirent_get_inode(struct dirent *dir)
{
	return inode_lookup(&dir->ino);
}

