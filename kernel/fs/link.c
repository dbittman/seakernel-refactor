#include <fs/sys.h>
#include <fs/inode.h>
#include <fs/filesystem.h>
#include <fs/stat.h>
#include <errno.h>
#include <printk.h>

int fs_link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	if(!S_ISDIR(node->mode))
		return -ENOTDIR;
	if(node->fs != target->fs)
		return -EXDEV;
	mutex_acquire(&node->lock);

	if(node->fs->driver->inode_ops->lookup(node, name, namelen, NULL) == 0) {
		mutex_release(&node->lock);
		return -EEXIST;
	}
	if(node != target)
		mutex_acquire(&target->lock); //TODO: do we need these locks?
	int ret = node->fs->driver->inode_ops->link(node, name, namelen, target);
	if(ret == 0)
		target->links++;
	inode_mark_dirty(target);
	if(node != target)
		mutex_release(&target->lock);
	mutex_release(&node->lock);
	return ret;
}

int fs_unlink(struct inode *node, const char *name, size_t namelen)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	if(!S_ISDIR(node->mode))
		return -ENOTDIR;
	struct dirent *dir = dirent_lookup(node, name, namelen);
	if(!dir) {
		return -ENOENT;
	}
	mutex_acquire(&node->lock);
	struct inode *target = dirent_get_inode(dir);
	if(!target) {
		mutex_release(&node->lock);
		dirent_put(dir);
		return -EIO;
	}
	if(S_ISDIR(target->mode)) {
		mutex_release(&node->lock);
		inode_put(target);
		dirent_put(dir);
		return -EISDIR;
	}
	if(target != node)
		mutex_acquire(&target->lock);
	dir->flags |= DIRENT_UNLINK;
	if(target != node)
		mutex_release(&target->lock);
	mutex_release(&node->lock);
	inode_put(target);
	dirent_put(dir);
	return 0;
}

static bool __directory_empty(struct inode *node)
{
	char buffer[512];
	_Atomic size_t zero = 0;
	int ret = node->fs->driver->inode_ops->getdents(node, &zero, (void *)buffer, 512);
	struct gd_dirent *gd = (void *)buffer;
	while((char *)gd < buffer + ret) {
		struct dirent *dir = dirent_lookup_cached(node, gd->d_name, strlen(gd->d_name));
		if(dir && !(dir->flags & DIRENT_UNLINK)) {
			dirent_put(dir);
			if(strcmp(gd->d_name, ".")
					&& strcmp(gd->d_name, ".."))
				return false;
		}
		if(dir)
			dirent_put(dir);
		gd = (void *)((char *)gd + gd->d_reclen);
	}
	return true;
}

int fs_rmdir(struct inode *node, const char *name, size_t namelen)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	if(!S_ISDIR(node->mode))
		return -ENOTDIR;
	struct dirent *dir = dirent_lookup(node, name, namelen);
	mutex_acquire(&node->lock);
	struct inode *target = dirent_get_inode(dir);
	if(!S_ISDIR(target->mode)) {
		inode_put(target);
		mutex_release(&node->lock);
		dirent_put(dir);
		return -ENOTDIR;
	}
	
	if(target != node)
		mutex_acquire(&target->lock);
	if(!__directory_empty(target)) {
		if(target != node)
			mutex_release(&target->lock);
		inode_put(target);
		mutex_release(&node->lock);
		dirent_put(dir);
		return -ENOTEMPTY;
	}
	int ret = -ENOTSUP;
	if(target->fs->driver->inode_ops->unlink)
		ret = target->fs->driver->inode_ops->unlink(target, ".", 1);
	
	if(ret != 0) {
		if(target != node)
			mutex_release(&target->lock);
		inode_put(target);
		mutex_release(&node->lock);
		dirent_put(dir);
		return ret;
	}
	target->links--;
	inode_mark_dirty(target);
	if(target->fs->driver->inode_ops->unlink)
		ret = target->fs->driver->inode_ops->unlink(target, "..", 2);
	if(ret != 0) {
		if(target != node)
			mutex_release(&target->lock);
		inode_put(target);
		mutex_release(&node->lock);
		dirent_put(dir);
		return ret;
	}
	node->links--;
	dir->flags |= DIRENT_UNLINK;
	inode_mark_dirty(node);
	if(target != node)
		mutex_release(&target->lock);
	inode_put(target);
	mutex_release(&node->lock);
	dirent_put(dir);
	return 0;
}

