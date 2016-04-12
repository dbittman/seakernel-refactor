#include <file.h>
#include <process.h>
#include <thread.h>
#include <fs/inode.h>
#include <fs/dirent.h>
#include <fcntl.h>
#include <errno.h>

static void _file_put(void *obj)
{
	struct file *file = obj;
	kobj_putref(file->dirent);
}

struct kobj kobj_file = {
	.name = "file",
	.size = sizeof(struct file),
	.initialized = false,
	.init = NULL,
	.create = NULL,
	.put = _file_put,
	.destroy = NULL,
};

int process_allocate_fd(struct file *file)
{
	spinlock_acquire(&current_thread->process->files_lock);
	struct process *proc = current_thread->process;
	for(int i=0;i<MAX_FD;i++) {
		if(proc->files[i].file == NULL) {
			proc->files[i].file = kobj_getref(file);
			proc->files[i].flags = 0;
			spinlock_release(&current_thread->process->files_lock);
			return i;
		}
	}
	spinlock_release(&current_thread->process->files_lock);
	return -1;
}

void process_release_fd(int fd)
{
	spinlock_acquire(&current_thread->process->files_lock);
	struct file *file = current_thread->process->files[fd].file;
	current_thread->process->files[fd].file = NULL;
	kobj_putref(file);
	spinlock_release(&current_thread->process->files_lock);
}

struct file *process_get_file(int fd)
{
	if(fd >= MAX_FD)
		return NULL;
	spinlock_acquire(&current_thread->process->files_lock);
	struct file *file = current_thread->process->files[fd].file;
	spinlock_release(&current_thread->process->files_lock);
	return kobj_getref(file);
}

void file_close(struct file *file)
{
	struct inode *node = file_get_inode(file);
	if(node->ops && node->ops->close)
		node->ops->close(file, node);
	inode_put(node);
	kobj_putref(file);
}

void process_copy_files(struct process *from, struct process *to)
{
	spinlock_acquire(&from->files_lock);
	for(int i=0;i<MAX_FD;i++) {
		if(from->files[i].file) {
			to->files[i].file = kobj_getref(from->files[i].file);
			to->files[i].flags = from->files[i].flags;

			struct inode *node = file_get_inode(to->files[i].file);
			if(node->ops && node->ops->open)
				node->ops->open(to->files[i].file, node);
			inode_put(node);
		}
	}
	spinlock_release(&from->files_lock);
}

void process_close_files(struct process *proc, bool all)
{
	spinlock_acquire(&proc->files_lock);
	for(int i=0;i<MAX_FD;i++) {
		if(proc->files[i].file) {
			if((proc->files[i].flags & FD_CLOEXEC) || all) {
				file_close(proc->files[i].file);
				proc->files[i].file = NULL;
				proc->files[i].flags = 0;
			}
		}
	}
	spinlock_release(&proc->files_lock);
}

ssize_t file_read(struct file *f, size_t off, size_t len, char *buf)
{
	struct inode *ino = file_get_inode(f);
	if(!ino)
		return -1;

	ssize_t ret = -EIO;
	if(ino->ops && ino->ops->write)
		ret = ino->ops->read(f, ino, off, len, buf);
	inode_put(ino);
	return ret;
}

ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf)
{
	struct inode *ino = file_get_inode(f);
	if(!ino)
		return -1;

	ssize_t ret = -EIO;
	if(ino->ops && ino->ops->write)
		ret = ino->ops->write(f, ino, off, len, buf);
	inode_put(ino);
	return ret;
}

int file_truncate(struct file *f, size_t len)
{
	f->pos = len;
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

