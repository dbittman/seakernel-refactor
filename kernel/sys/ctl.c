#include <fs/sys.h>
#include <file.h>
#include <errno.h>
#include <thread.h>
#include <process.h>
sysret_t sys_fcntl(int fd, int cmd, long arg)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;

	long ret = 0;
	switch(cmd) {
		case F_GETFD:
			ret = current_thread->process->files[fd].flags;
			break;
		case F_SETFD:
			current_thread->process->files[fd].flags = arg;
			break;
		case F_SETFL:
			file->flags = (arg & ~(F_READ | F_WRITE)) | (arg & (F_READ | F_WRITE));
			break;
		case F_GETFL:
			ret = file->flags;
			break;

		default:
			ret = -EINVAL;
	}
	kobj_putref(file);
	return ret;
}

sysret_t sys_ioctl(int fd, int cmd, long arg)
{
	struct file *file = process_get_file(fd);
	if(!file)
		return -EBADF;
	long ret = -ENOTSUP;
	if(file->ops->ioctl)
		ret = file->ops->ioctl(file, cmd & 0xFFFFFFFF /* TODO */, arg);
	kobj_putref(file);
	return ret;
}

