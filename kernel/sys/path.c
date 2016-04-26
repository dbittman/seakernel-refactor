#include <fs/path.h>
#include <fs/dirent.h>
#include <thread.h>
#include <process.h>
#include <device.h>
#include <file.h>
#include <errno.h>
#include <block.h>
#include <fs/filesystem.h>
#include <printk.h>
#include <fs/sys.h>
#include <string.h>
/* TODO: all the file_get_inode calls can return NULL! */

static void _stat(struct inode *node, struct stat *buf)
{
	buf->st_dev = makedev(node->major, node->minor);
	buf->st_ino = node->id.inoid;
	buf->st_nlink = node->links;
	buf->st_mode = node->mode;
	buf->st_uid = node->uid;
	buf->st_gid = node->gid;
	buf->st_size = node->length;
	/* HACK */
	buf->st_blksize = 512;
	buf->st_blocks = node->length / 512;
	buf->st_atim.tv_sec = node->atime;
	buf->st_mtim.tv_sec = node->mtime;
	buf->st_ctim.tv_sec = node->ctime;
}

sysret_t sys_stat(const char *path, struct stat *buf)
{
	struct inode *node;
	int ret = fs_path_resolve(path, NULL, 0, 0, NULL, &node);
	if(ret < 0)
		return ret;
	_stat(node, buf);
	inode_put(node);
	return 0;
}

sysret_t sys_chdir(const char *path)
{
	struct inode *node;
	int err = fs_path_resolve(path, NULL, 0, 0, NULL, &node);
	if(err < 0)
		return err;

	if(!S_ISDIR(node->mode)) {
		inode_put(node);
		return -ENOTDIR;
	}

	struct inode *old = atomic_exchange(&current_thread->process->cwd, node);
	inode_put(old);

	return 0;
}

sysret_t sys_fchdir(int fd)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!file->dirent)
		return -EINVAL;
	struct inode *node = file_get_inode(file);
	if(!S_ISDIR(node->mode)) {
		inode_put(node);
		kobj_putref(file);
		return -ENOTDIR;
	}

	struct inode *old = atomic_exchange(&current_thread->process->cwd, node);
	inode_put(old);

	kobj_putref(file);
	return 0;
}

/* TODO: store root as dirent, like in cwd */
sysret_t sys_chroot(const char *path)
{
	if(current_thread->process->euid != 0)
		return -EPERM;
	struct inode *node;
	int err = fs_path_resolve(path, NULL, 0, 0, NULL, &node);
	if(err < 0)
		return err;

	struct inode *old = atomic_exchange(&current_thread->process->root, node);
	inode_put(old);
	return 0;
}

sysret_t sys_access(const char *path, int mode)
{
	struct inode *node;
	int ret = fs_path_resolve(path, NULL, 0, 0, NULL, &node);
	if(ret < 0)
		return ret;

	bool ok = true;
	if(mode & 4)
		ok = ok && inode_check_access(node, PERM_READ);
	if(mode & 2)
		ok = ok && inode_check_access(node, PERM_WRITE);
	if(mode & 1)
		ok = ok && inode_check_access(node, PERM_EXEC);

	inode_put(node);
	return 0;
}

sysret_t sys_lstat(const char *path, struct stat *buf)
{
	struct inode *node;
	int ret = fs_path_resolve(path, NULL, PATH_NOFOLLOW, 0, NULL, &node);
	if(ret < 0)
		return ret;
	_stat(node, buf);
	inode_put(node);
	return 0;
}

sysret_t sys_fstat(int fd, struct stat *buf)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	struct inode *node = file_get_inode(file);
	if(node) {
		_stat(node, buf);
		inode_put(node);
	} else {
		/* TODO: what to do here? */
		buf->st_mode = S_IFIFO;
	}
	kobj_putref(file);
	return 0;
}

sysret_t sys_getdents(int fd, struct gd_dirent *dp, int count)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	int ret = 0;
	struct inode *node = file_get_inode(file);
	if(!node->fs) {
		ret = -EINVAL;
		goto out;
	}
	if(!S_ISDIR(node->mode)) {
		ret = -ENOTDIR;
		goto out;
	}
	if(!inode_check_perm(node, PERM_EXEC)) {
		ret = -EACCES;
		goto out;
	}
	mutex_acquire(&node->lock);
	ret = node->fs->driver->inode_ops->getdents(node, &file->pos, dp, count);
	mutex_release(&node->lock);
out:
	inode_put(node);
	kobj_putref(file);
	return ret;
}

#include <processor.h>
sysret_t sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data)
{
	(void)data;
	struct inode *snode = NULL, *tnode;
	int err = 0;
	err = source ? fs_path_resolve(source, NULL, 0, 0, NULL, &snode) : 0;
	if(err < 0)
		return err;

	err = fs_path_resolve(target, NULL, 0, 0, NULL, &tnode);
	if(err < 0) {
		if(snode)
			inode_put(snode);
		return err;
	}

	struct filesystem *fs = NULL;
	if(!(flags & MS_BIND)) {
		struct blockdev *bd = NULL;
		if(snode) {
			bd = blockdev_get(snode->major, snode->minor);
			if(!bd && !(flags & MS_BIND)) {
				if(snode)
					inode_put(snode);
				inode_put(tnode);
				return -ENOTBLK;
			}
		}

		fs = fs_load_filesystem(bd, fstype, flags, &err);
		if(bd)
			kobj_putref(bd);
		if(!fs) {
			if(snode)
				inode_put(snode);
			inode_put(tnode);
			return err;
		}
	} else if(snode != NULL) {
		fs = kobj_getref(snode->fs);
	}
	err = -EINVAL;
	if(fs)
		err = fs_mount(tnode, fs);
	if(err) {
		fs_unload_filesystem(fs);
		inode_put(tnode);
		if(snode)
			inode_put(snode);
		return err;
	}

	return 0;
}

ssize_t sys_readlink(const char *path, char *buf, size_t bufsz)
{
	struct inode *node;
	int err = fs_path_resolve(path, NULL, PATH_NOFOLLOW, 0, NULL, &node);
	if(err < 0)
		return err;

	if(!S_ISLNK(node->mode)) {
		inode_put(node);
		return -EINVAL;
	}
	err = node->fs->driver->inode_ops->readlink(node, buf, bufsz);
	inode_put(node);
	return err ? err : (ssize_t)strlen(buf);
}

sysret_t sys_unlinkat(int dirfd, const char *_path, int flags)
{
	if(strlen(_path) > 255)
		return -ENAMETOOLONG;
	char path[256];
	memset(path, 0, sizeof(path));
	memcpy(path, _path, strlen(_path));

	struct inode *start = __get_at_start(dirfd);

	char *sep = strrchrm(path, '/');
	char *name;
	if(sep) {
		*sep = 0;
		name = sep + 1;
		struct inode *parent;
		int err = fs_path_resolve(path, start, 0, 0, NULL, &parent);
		inode_put(start);
		if(err < 0) {
			return err;
		}
		start = parent;
	} else {
		name = path;
	}

	int err;
	if((flags & AT_REMOVEDIR))
		err = fs_rmdir(start, name, strlen(name));
	else
		err = fs_unlink(start, name, strlen(name));
	inode_put(start);
	return err;
}

sysret_t sys_unlink(const char *path)
{
	return sys_unlinkat(AT_FDCWD, path, 0);
}

sysret_t sys_rmdir(const char *path)
{
	return sys_unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

sysret_t sys_link(const char *targ, const char *_path)
{
	if(strlen(_path) > 255)
		return -ENAMETOOLONG;
	char path[256];
	memset(path, 0, sizeof(path));
	memcpy(path, _path, strlen(_path));
	char *sep = strrchrm(path, '/');
	struct inode *parent;
	char *name;
	if(sep) {
		*sep = 0;
		int err = fs_path_resolve(path, NULL, 0, 0, NULL, &parent);
		if(err < 0)
			return err;
		name = sep + 1;
	} else {
		parent = kobj_getref(current_thread->process->cwd);
		name = path;
	}

	struct inode *targetnode;
	int err = fs_path_resolve(targ, NULL, 0, 0, NULL, &targetnode);
	if(err < 0) {
		inode_put(parent);
		return err;
	}
	if(S_ISDIR(targetnode->mode)) {
		inode_put(targetnode);
		inode_put(parent);
		return -EPERM;
	}
	err = fs_link(parent, name, strlen(name), targetnode);
	inode_put(targetnode);
	inode_put(parent);

	return err;
}

sysret_t sys_symlink(const char *target, const char *linkpath)
{
	struct inode *link;
	int err = fs_path_resolve(linkpath, NULL, PATH_CREATE | PATH_NOFOLLOW, S_IFLNK | 0777, NULL, &link);
	if(err < 0)
		return err;
	if(!(err & PATH_DID_CREATE)) {
		inode_put(link);
		return -EEXIST;
	}

	err = link->fs->driver->inode_ops->writelink(link, target);
	inode_put(link);
	return err;
}
