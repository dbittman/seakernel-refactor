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

static bool __directory_empty(struct inode *node)
{
	char buffer[512];
	_Atomic size_t zero = 0;
	int ret = node->fs->driver->inode_ops->getdents(node, &zero, (void *)buffer, 512);
	struct gd_dirent *gd = (void *)buffer;
	while((char *)gd < buffer + ret) {
		struct dirent *dir = dirent_lookup_cached(node, gd->d_name, strlen(gd->d_name));
		bool real = dir && !(dir->flags & DIRENT_UNLINK);
		if(dir)
			dirent_put(dir);
		if(real) {
			if(strcmp(gd->d_name, ".")
					&& strcmp(gd->d_name, "..")) {
				return false;
			}
		}
		gd = (void *)((char *)gd + gd->d_reclen);
	}
	return true;
}

static int __unlink(struct dirent *dir, bool allow_dir, bool check_empty)
{
	int ret = 0;
	struct inode *target = dirent_get_inode(dir);
	if(!target)
		return -EIO;
	
	if(S_ISDIR(target->mode) && !allow_dir)
		ret = -EISDIR;
	else if(S_ISDIR(target->mode) && check_empty && !__directory_empty(target))
		ret = -ENOTEMPTY;
	else
		dir->flags |= DIRENT_UNLINK;

	inode_put(target);
	return ret;
}

int fs_unlink(struct inode *node, const char *name, size_t namelen)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	if(!S_ISDIR(node->mode)) {
		printk("::%ld %ld %o %d %x\n", node->id.inoid, node->_header._koh_refs, node->mode, node->links, node->flags);
		return -ENOTDIR;
	}
	struct dirent *dir = dirent_lookup(node, name, namelen);
	if(!dir) {
		return -ENOENT;
	}
	mutex_acquire(&node->lock);
	int ret = __unlink(dir, false, false);
	mutex_release(&node->lock);
	dirent_put(dir);
	return ret;
}

int fs_rmdir(struct inode *node, const char *name, size_t namelen)
{
	if(!inode_check_perm(node, PERM_WRITE))
		return -EACCES;
	if(!S_ISDIR(node->mode))
		return -ENOTDIR;

	int ret = 0;
	struct dirent *dir = dirent_lookup(node, name, namelen);
	if(!dir)
		return -ENOENT;
	struct inode *target = dirent_get_inode(dir);
	if(!S_ISDIR(target->mode)) {
		inode_put(target);
		dirent_put(dir);
		return -ENOTDIR;
	}
	mutex_acquire(&node->lock);

	struct dirent *pp = dirent_lookup(target, "..", 2);
	if(!pp) {
		ret = -EIO; //TODO: whats the actual error here?
	} else {
		struct dirent *sp = dirent_lookup(target, ".", 1);
		if(!sp) {
			ret = -EIO;
		} else {
			if((ret = __unlink(dir, true, true)) == 0) {
				__unlink(pp, true, false);
				__unlink(sp, true, false);
			}
			dirent_put(sp);
		}
		dirent_put(pp);
	}

	mutex_release(&node->lock);
	inode_put(target);
	dirent_put(dir);
	return ret;
}

