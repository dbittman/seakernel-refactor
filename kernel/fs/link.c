#include <fs/sys.h>
#include <fs/inode.h>
#include <fs/filesystem.h>
#include <fs/stat.h>
#include <errno.h>

int fs_link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	mutex_acquire(&node->lock);
	mutex_acquire(&target->lock); //TODO: do we need these locks?
	target->links++;
	int ret = node->fs->driver->inode_ops->link(node, name, namelen, target);
	mutex_release(&target->lock);
	mutex_release(&node->lock);
	return ret;
}

