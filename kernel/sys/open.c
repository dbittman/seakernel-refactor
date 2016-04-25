#include <fs/inode.h>
#include <fs/dirent.h>
#include <file.h>
#include <fs/path.h>
#include <fs/sys.h>
#include <printk.h>
#include <errno.h>
#include <fcntl.h>
#include <thread.h>
#include <process.h>
#include <device.h>

sysret_t sys_open(const char *path, int flags, int mode)
{
	flags++;
	mode = (mode & ~0xFFF) | ((mode & 0xFFF) & (~(current_thread->process->cmask & 0xFFF)));
	struct dirent *dir;
	struct inode *node;
	int pathfl = 0;
	if(flags & O_CREAT)
		pathfl |= PATH_CREATE;
	if(((flags & O_CREAT) && (flags & O_EXCL)) || (flags & O_NOFOLLOW))
		pathfl |= PATH_NOFOLLOW;
	int res = fs_path_resolve(path, 0, pathfl, mode, &dir, &node);

	if(res < 0)
		return res;

	if((flags & O_NOFOLLOW) && (S_ISLNK(node->mode))) {
		kobj_putref(dir);
		inode_put(node);
		return -ELOOP;
	}

	if((flags & O_EXCL) && (flags & O_CREAT) && !(res & PATH_DID_CREATE)) {
		kobj_putref(dir);
		inode_put(node);
		return -EEXIST;
	}

	if((flags & F_WRITE) && !inode_check_perm(node, PERM_WRITE)) {
		kobj_putref(dir);
		inode_put(node);
		return -EACCES;
	}

	if((flags & F_READ) && !inode_check_perm(node, PERM_READ)) {
		kobj_putref(dir);
		inode_put(node);
		return -EACCES;
	}

	struct file *file = file_create(dir, FDT_UNKNOWN);
	file->pos = 0;
	file->flags = flags;

	int fd = process_allocate_fd(file, 0);
	if(fd < 0) {
		inode_put(node);
		kobj_putref(file);
		return -EMFILE;
	}

	if(flags & O_CLOEXEC)
		current_thread->process->files[fd].flags |= FD_CLOEXEC;

	if((flags & O_TRUNC) && (flags & F_WRITE))
		file_truncate(file, 0);


	inode_put(node);
	kobj_putref(file);
	return fd;
}

sysret_t sys_dup(int old)
{
	struct file *file = process_get_file(old);
	if(!file)
		return -EBADF;

	int nf = process_allocate_fd(file, 0);
	kobj_putref(file);
	return nf;
}

sysret_t sys_dup2(int old, int new)
{
	struct file *file = process_get_file(old);
	if(!file)
		return -EBADF;

	struct file *of = process_exchange_fd(file, new);
	kobj_putref(file);
	if(of)
		file_close(of);
	return new;
}

sysret_t sys_mknod(const char *path, int mode, dev_t dev)
{
	int fd = sys_open(path, O_CREAT | O_WRONLY | O_EXCL, mode);
	if(fd < 0)
		return fd;

	struct file *file = process_get_file(fd);
	struct inode *node = file_get_inode(file);
	node->major = major(dev);
	node->minor = minor(dev);
	inode_mark_dirty(node);
	inode_put(node);
	kobj_putref(file);
	sys_close(fd);
	return 0;
}

sysret_t sys_close(int fd)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	process_release_fd(fd);
	file_close(file);
	return 0;
}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

sysret_t sys_lseek(int fd, ssize_t off, int whence)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;

	if(file->devtype == FDT_FIFO || file->devtype == FDT_SOCK) {
		kobj_putref(file);
		return -ESPIPE;
	}

	int ret = 0;
	switch(whence) {
		ssize_t pos;
		case SEEK_SET:
			if(off < 0)
				ret = -EINVAL;
			else
				file->pos = off;
			break;
		case SEEK_CUR:
			pos = file->pos;
			if(pos + off < 0)
				ret = -EINVAL;
			else
				file->pos = off + pos;
			break;
		case SEEK_END: 
			{
				struct inode *node = file_get_inode(file);
				pos = node->length;
				if((ssize_t)pos + off < 0)
					ret = -EINVAL;
				else
					file->pos = pos + off;
				inode_put(node);
			} break;
	}
	kobj_putref(file);
	return ret ? ret : (ssize_t)file->pos;
}

sysret_t sys_mkdir(const char *path, int mode)
{
	int f = sys_open(path, O_CREAT | O_RDWR | O_EXCL, S_IFDIR | (mode & 0777));
	if(f < 0)
		return f;
	sys_close(f);
	return 0;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;

	if(!(file->flags & F_READ)) {
		kobj_putref(file);
		return -EACCES;
	}
	
	ssize_t amount = file_read(file, file->pos, count, buf);
	if(amount > 0) {
		file->pos += amount;
	}
	kobj_putref(file);
	return amount;
}

ssize_t sys_write(int fd, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_WRITE)) {
		kobj_putref(file);
		return -EACCES;
	}

	size_t off = (file->flags & O_APPEND) ? file_get_len(file) : file->pos;
	file->pos = off;
	ssize_t amount = file_write(file, off, count, buf);
	if(amount > 0) {
		file->pos += amount;
	}
	kobj_putref(file);
	return amount;
}

ssize_t sys_pread(int fd, void *buf, size_t count, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_READ)) {
		kobj_putref(file);
		return -EACCES;
	}

	ssize_t amount = file_read(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

ssize_t sys_pwrite(int fd, void *buf, size_t count, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_WRITE)) {
		kobj_putref(file);
		return -EACCES;
	}

	ssize_t amount = file_write(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_WRITE)) {
		kobj_putref(file);
		return -EACCES;
	}

	size_t amount = 0;
	for(int i=0;i<iovc;i++) {
		if(!iov[i].len)
			continue;
		ssize_t thisamount = file_write(file, off, iov[i].len, iov[i].base);
		if(thisamount < 0) {
			kobj_putref(file);
			if(amount == 0)
				return thisamount;
			return amount;
		}
		amount += thisamount;
		off += thisamount;
	}
	kobj_putref(file);
	return amount;
}

ssize_t sys_preadv(int fd, struct iovec *iov, int iovc, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_READ)) {
		kobj_putref(file);
		return -EACCES;
	}

	size_t amount = 0;
	for(int i=0;i<iovc;i++) {
		if(!iov[i].len)
			continue;
		ssize_t thisamount = file_read(file, off, iov[i].len, iov[i].base);
		if(thisamount < 0) {
			kobj_putref(file);
			if(amount == 0)
				return thisamount;
			return amount;
		}
		amount += thisamount;
		off += thisamount;
	}
	kobj_putref(file);
	return amount;
}

ssize_t sys_writev(int fd, struct iovec *iov, int iovc)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_WRITE)) {
		kobj_putref(file);
		return -EACCES;
	}

	size_t off = (file->flags & O_APPEND) ? file_get_len(file) : file->pos;
	file->pos = off;
	ssize_t res = sys_pwritev(fd, iov, iovc, off);
	if(res > 0)
		file->pos += res;
	kobj_putref(file);
	return res;
}

ssize_t sys_readv(int fd, struct iovec *iov, int iovc)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	if(!(file->flags & F_READ)) {
		kobj_putref(file);
		return -EACCES;
	}

	ssize_t res = sys_preadv(fd, iov, iovc, file->pos);
	if(res > 0)
		file->pos += res;
	kobj_putref(file);
	return res;
}

sysret_t sys_fadvise(int fd, ssize_t offset, size_t len, int advice)
{
	(void)fd;
	(void)offset;
	(void)len;
	(void)advice;
	return 0;
}

