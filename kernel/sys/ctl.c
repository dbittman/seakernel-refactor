#include <fs/sys.h>
#include <file.h>
#include <errno.h>
#include <thread.h>
#include <process.h>
#include <printk.h>
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
		case F_DUPFD:
			ret = process_allocate_fd(file, (int)arg);
			if(ret >= 0)
				process_copy_proc_fd(current_thread->process, current_thread->process, fd, ret);
			break;
		case F_SETOWN: case F_GETOWN: break;

		/* for now, pretend we can lock */
		case F_GETLK: {
			struct flock *flock = (void *)arg;
			flock->l_type = F_UNLCK;
		} break;

		case F_SETLKW: case F_SETLK: break;
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

