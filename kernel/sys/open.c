#include <fs/inode.h>
#include <fs/dirent.h>
#include <file.h>
#include <fs/path.h>

int sys_open(const char *path, int flags, int mode)
{
	(void)flags;
	(void)mode;
	struct dirent *dir = fs_path_resolve(path, 0);

	struct inode *node = dirent_get_inode(dir);
	
	kobj_putref(node);

	struct file *file = kobj_allocate(&kobj_file);
	file->dirent = dir;

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

