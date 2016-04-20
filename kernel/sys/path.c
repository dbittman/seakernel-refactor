#include <fs/path.h>
#include <fs/dirent.h>
#include <thread.h>
#include <process.h>
#include <device.h>
#include <file.h>
#include <errno.h>
#include <fs/filesystem.h>
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

sysret_t sys_fstat(int fd, struct stat *buf)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	struct inode *node = file_get_inode(file);
	kobj_putref(file);
	_stat(node, buf);
	inode_put(node);
	return 0;
}

sysret_t sys_getcwd(char *buf, size_t size)
{
	*buf = '/';
	*(buf+1) = 0;
	(void)size;
	return 0;
	/*
	struct dirent *dir = kobj_getref(current_thread->process->cwd);
	struct inode *node = dirent_get_inode(dir);

	if(dir->namelen+1 >= size) {
		kobj_putref(dir);
		inode_put(node);
		return -ERANGE;
	}

	memcpy(buf, dir->name, dir->namelen);
	*(buf + dir->namelen) = '/';
	int pos = dir->namelen + 1;
	kobj_putref(dir);

	while(1) {
		struct inode *next_inode;
		struct dirent *next_dirent;
		int err = fs_path_resolve("..", node, 0, 0, &next_dirent, &next_node);
		inode_put(node);
		if(err) {
			return err;
		}
		
		memcpy(buf+pos, next_dirent->name, next_dirent->namelen);
		*(buf+pos+next_dirent->namelen) = '/';

		node = next_inode;
	}
	*/
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
	ret = node->fs->driver->inode_ops->getdents(node, file->pos, dp, count);
	if(ret > 0)
		file->pos += ret;
out:
	inode_put(node);
	kobj_putref(file);
	return ret;
}

