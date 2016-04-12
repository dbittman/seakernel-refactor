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

int sys_open(const char *path, int flags, int mode)
{
	flags++;
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


	struct file *file = kobj_allocate(&kobj_file);
	file->dirent = dir;
	file->pos = 0;
	file->flags = flags;

	int fd = process_allocate_fd(file);
	if(fd < 0) {
		inode_put(node);
		kobj_putref(dir);
		kobj_putref(file);
		return -EMFILE;
	}

	current_thread->process->files[fd].flags |= FD_CLOEXEC;

	if((flags & O_TRUNC) && (flags & F_WRITE))
		file_truncate(file, 0);

	if(node->ops && node->ops->open)
		node->ops->open(file, node);

	inode_put(node);
	kobj_putref(file);
	return fd;
}

int sys_close(int fd)
{
	struct file *file = process_get_file(fd);
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
		ssize_t thisamount = file_write(file, off, iov[iovc].len, iov->base);
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
		ssize_t thisamount = file_read(file, off, iov[iovc].len, iov->base);
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

