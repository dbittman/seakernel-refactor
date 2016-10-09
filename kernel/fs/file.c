#include <file.h>
#include <process.h>
#include <thread.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <fs/stat.h>
#include <device.h>
#include <fs/sys.h>
#include <printk.h>

static void _file_put(void *obj)
{
	struct file *file = obj;
	if(file->ops && file->ops->destroy)
		file->ops->destroy(file);
	if(file->dirent)
		dirent_put(file->dirent);
}

static void _file_init(void *obj)
{
	struct file *file = obj;
	file->dirent = NULL;
	file->ops = NULL;
	file->devtype = FDT_UNKNOWN;
	file->devdata = NULL;
}

static void _file_create(void *obj)
{
	_file_init(obj);
}

struct kobj kobj_file = {
	KOBJ_DEFAULT_ELEM(file),
	.init = _file_init,
	.create = _file_create,
	.put = _file_put,
	.destroy = NULL,
};

struct file_calls *file_get_ops(struct inode *node)
{
	if(S_ISCHR(node->mode) || S_ISBLK(node->mode))
		return dev_get_fops(node);
	else if(S_ISFIFO(node->mode))
		return &pipe_fops;
	else if(S_ISSOCK(node->mode))
		return &socket_fops;
	else
		return &fs_fops;
}

void process_remove_proc_fd(struct process *proc, int fd)
{
	if(proc == kernel_process)
		return;

	char str[128];
	snprintf(str, 128, "/proc/%d/fd/%d", proc->pid, fd);
	int r = sys_unlink(str);
	/* TODO: we need to seriously evaluate things like this.
	 * This could totally fail, for real reasons (EINTR). */
	assertmsg(r == 0, "%d", r);
}

void process_create_proc_fd(struct process *proc, int fd, const char *path)
{
	if(proc == kernel_process)
		return;
	char str[128];
	snprintf(str, 128, "/proc/%d/fd/%d", proc->pid, fd);
	int r = sys_symlink(path, str);
	if(r == -17) {
		char tmp[256];
		int x = sys_readlink(str, tmp, 255);
		printk(":: %s %d %s\n", str, x, tmp);
	}
	assertmsg(r == 0, "%d (%d)", r, fd);
}

void process_copy_proc_fd(struct process *from, struct process *to, int fromfd, int tofd)
{
	if(from == kernel_process || to == kernel_process)
		return;
	char fp[128];
	snprintf(fp, 128, "/proc/%d/fd/%d", from->pid, fromfd);
	char tp[128];
	snprintf(tp, 128, "/proc/%d/fd/%d", to->pid, tofd);

	char link[256];
	int r = sys_readlink(fp, link, 256);
	assertmsg(r >= 0, "%d", r);
	r = sys_symlink(link, tp);
	assertmsg(r == 0, "%d", r);
}

struct file *file_create(struct dirent *dir, enum file_device_type type)
{
	struct file *file = kobj_allocate(&kobj_file);
	if(dir) {
		struct inode *node = dirent_get_inode(dir);
		if(node) {
			if(type == FDT_UNKNOWN)
				file->ops = file_get_ops(node);
			inode_put(node);
		} else {
			kobj_putref(file);
			return NULL;
		}
		file->dirent = dir;
	}
	file->devtype = type;
	if(type != FDT_UNKNOWN) {
		switch(type) {
			case FDT_REG:
				file->ops = &fs_fops;
				break;
			case FDT_FIFO:
				file->ops = &pipe_fops;
				break;
			case FDT_SOCK:
				file->ops = &socket_fops;
				break;
			case FDT_CHAR: case FDT_BLOCK:
				panic(0, "cannot set file_device_type to char or block without node");
			case FDT_UNKNOWN:
				panic(0, "shouldn't get here");
		}
	}
	file->flags = 0;
	file->pos = 0;
	if(file->ops && file->ops->create)
		file->ops->create(file);
	return file;
}

struct file *process_exchange_fd(struct file *file, int i)
{
	if(i >= MAX_FD || i < 0)
		return NULL;
	spinlock_acquire(&current_thread->process->files_lock);
	struct process *proc = current_thread->process;
	struct file *of = proc->files[i].file;
	proc->files[i].file = kobj_getref(file);
	if(file->ops && file->ops->open)
		file->ops->open(file);
	spinlock_release(&current_thread->process->files_lock);
	return of;
}

int process_allocate_fd(struct file *file, int start)
{
	if(start < 0)
		return -EINVAL;
	spinlock_acquire(&current_thread->process->files_lock);
	struct process *proc = current_thread->process;
	for(int i=start;i<MAX_FD;i++) {
		if(proc->files[i].file == NULL) {
			proc->files[i].file = kobj_getref(file);
			proc->files[i].flags = 0;
			if(file->ops && file->ops->open)
				file->ops->open(file);
			spinlock_release(&current_thread->process->files_lock);
			return i;
		}
	}
	spinlock_release(&current_thread->process->files_lock);
	return -EMFILE;
}

void process_release_fd(int fd)
{
	spinlock_acquire(&current_thread->process->files_lock);
	struct file *file = current_thread->process->files[fd].file;
	current_thread->process->files[fd].file = NULL;
	kobj_putref(file);
	process_remove_proc_fd(current_thread->process, fd);
	spinlock_release(&current_thread->process->files_lock);
}

struct file *process_get_file(int fd)
{
	if(fd >= MAX_FD || fd < 0)
		return NULL;
	spinlock_acquire(&current_thread->process->files_lock);
	struct file *file = current_thread->process->files[fd].file;
	if(file) kobj_getref(file);
	spinlock_release(&current_thread->process->files_lock);
	return file;
}

void file_close(struct file *file)
{
	if(file->ops && file->ops->close)
		file->ops->close(file);
	kobj_putref(file);
}

void process_copy_files(struct process *from, struct process *to)
{
	spinlock_acquire(&from->files_lock);
	for(int i=0;i<MAX_FD;i++) {
		if(from->files[i].file) {
			to->files[i].file = kobj_getref(from->files[i].file);
			to->files[i].flags = from->files[i].flags;

			process_copy_proc_fd(from, to, i, i);
			struct file *f = to->files[i].file;
			if(f->ops && f->ops->open)
				f->ops->open(f);
		}
	}
	spinlock_release(&from->files_lock);
}

void process_close_files(struct process *proc, bool all)
{
	for(int i=0;i<MAX_FD;i++) {
		if(proc->files[i].file) {
			if((proc->files[i].flags & FD_CLOEXEC) || all) {
				file_close(proc->files[i].file);
				process_remove_proc_fd(proc, i);
				proc->files[i].file = NULL;
				proc->files[i].flags = 0;
			}
		}
	}
}

ssize_t file_read(struct file *f, size_t off, size_t len, char *buf)
{
	ssize_t ret = -EIO;
	if(f->ops && f->ops->read)
		ret = f->ops->read(f, off, len, buf);
	return ret;
}

ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf)
{
	ssize_t ret = -EIO;
	if(f->ops && f->ops->write)
		ret = f->ops->write(f, off, len, buf);
	return ret;
}

int file_truncate(struct file *f, size_t len)
{
	struct inode *node = file_get_inode(f);
	if(!node)
		return -EINVAL;
	node->length = len;
	if(f->pos > node->length)
		f->pos = node->length;
	inode_mark_dirty(node);
	inode_put(node);
	return 0;
}

size_t file_get_len(struct file *f)
{
	struct inode *ino = file_get_inode(f);
	if(!ino)
		return 0;
	size_t ret = ino->length;
	inode_put(ino);
	return ret;
}

