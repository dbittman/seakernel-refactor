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
	struct dirent *dir;
	int err = fs_path_resolve(path, NULL, 0, 0, &dir, &node);
	if(err < 0)
		return err;

	if(!S_ISDIR(node->mode)) {
		inode_put(node);
		kobj_putref(dir);
		return -ENOTDIR;
	}

	inode_put(node);
	struct dirent *old = atomic_exchange(&current_thread->process->cwd, dir);
	kobj_putref(old);

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

	inode_put(node);
	struct dirent *dir = atomic_exchange(&current_thread->process->cwd, kobj_getref(file->dirent));
	kobj_putref(dir);

	kobj_putref(file);
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
	int ret = fs_path_resolve(path, NULL, PATH_SYMLINK, 0, NULL, &node);
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
	ret = node->fs->driver->inode_ops->getdents(node, &file->pos, dp, count);
out:
	inode_put(node);
	kobj_putref(file);
	return ret;
}

sysret_t sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data)
{
	(void)data;
	struct inode *snode, *tnode;
	int err = fs_path_resolve(source, NULL, 0, 0, NULL, &snode);
	if(err < 0)
		return err;

	err = fs_path_resolve(target, NULL, 0, 0, NULL, &tnode);
	if(err < 0) {
		inode_put(snode);
		return err;
	}

	struct blockdev *bd = blockdev_get(snode->major, snode->minor);
	if(!bd) {
		inode_put(snode);
		inode_put(tnode);
		return -ENOTBLK;
	}

	struct filesystem *fs = fs_load_filesystem(bd, fstype, flags, &err);
	kobj_putref(bd);
	if(!fs) {
		inode_put(snode);
		inode_put(tnode);
		return err;
	}

	err = fs_mount(tnode, fs);
	if(err) {
		fs_unload_filesystem(fs);
		inode_put(tnode);
		inode_put(snode);
		return err;
	}

	return 0;
}

