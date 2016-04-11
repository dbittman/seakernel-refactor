#include <fs/inode.h>
#include <fs/dirent.h>
#include <file.h>
#include <fs/path.h>
#include <fs/sys.h>
#include <printk.h>
int sys_open(const char *path, int flags, int mode)
{
	struct dirent *dir;
	struct inode *node;
	fs_path_resolve(path, 0, (flags & O_CREAT) ? PATH_CREATE : 0, mode, &dir, &node);

	inode_put(node);

	struct file *file = kobj_allocate(&kobj_file);
	file->dirent = dir;
	file->pos = 0;

	int fd = process_allocate_fd(file);

	kobj_putref(file);
	return fd;
}

int sys_close(int fd)
{
	struct file *file = process_get_file(fd);
	process_release_fd(fd);
	kobj_putref(file);
	return 0;
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
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
	ssize_t amount = file_write(file, file->pos, count, buf);
	if(amount > 0) {
		file->pos += amount;
	}
	kobj_putref(file);
	return amount;
}

ssize_t sys_pread(int fd, size_t off, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
	ssize_t amount = file_read(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

ssize_t sys_pwrite(int fd, size_t off, void *buf, size_t count)
{
	struct file *file = process_get_file(fd);
	ssize_t amount = file_write(file, off, count, buf);
	kobj_putref(file);
	return amount;
}

