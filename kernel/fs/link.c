#include <fs/sys.h>
#include <fs/inode.h>
#include <fs/filesystem.h>

int fs_link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	target->links++;
	return node->fs->driver->inode_ops->link(node, name, namelen, target);
}

