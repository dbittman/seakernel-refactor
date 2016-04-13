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

int sys_open(const char *path, int flags, int mode)
{
	flags++;
	mode = (mode & ~0xFFF) | ((mode & 0xFFF) & (~(current_thread->process->cmask & 0xFFF)));
	struct dirent *dir;
	struct inode *node;
	int res = fs_path_resolve(path, 0, (flags & O_CREAT) ? PATH_CREATE : 0, mode, &dir, &node);

	if(res < 0)
		return res;

	if((flags & O_EXCL) && (flags & O_CREAT) && !(res & PATH_DID_CREATE)) {
		kobj_putref(dir);
		inode_put(node);
		return -EEXIST;
	}

	struct file *file = file_create(dir, NULL);
	file->pos = 0;
	file->flags = flags;

	int fd = process_allocate_fd(file);
	if(fd < 0) {
		inode_put(node);
		kobj_putref(dir);
		kobj_putref(file);
		return -EMFILE;
	}

	if(flags & O_CLOEXEC)
		current_thread->process->files[fd].flags |= FD_CLOEXEC;

	if((flags & O_TRUNC) && (flags & F_WRITE))
		file_truncate(file, 0);

	if(file->ops && file->ops->open)
		file->ops->open(file);

	inode_put(node);
	kobj_putref(file);
	return fd;
}

int sys_mknod(const char *path, int mode, dev_t dev)
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

int sys_close(int fd)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	process_release_fd(fd);
	file_close(file);
	return 0;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
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
	ssize_t amount = file_read(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

ssize_t sys_pwrite(int fd, void *buf, size_t count, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	ssize_t amount = file_write(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, size_t off)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	size_t amount = 0;
	for(int i=0;i<iovc;i++) {
		if(!iov[i].len)
			continue;
		ssize_t thisamount = file_write(file, off, iov[i].len, iov->base);
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
	size_t amount = 0;
	for(int i=0;i<iovc;i++) {
		if(!iov[i].len)
			continue;
		ssize_t thisamount = file_read(file, off, iov[i].len, iov->base);
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
	ssize_t res = sys_preadv(fd, iov, iovc, file->pos);
	if(res > 0)
		file->pos += res;
	kobj_putref(file);
	return res;
}

