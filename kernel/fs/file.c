#include <file.h>
#include <process.h>
#include <thread.h>
#include <fs/inode.h>
#include <fs/dirent.h>

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
	struct process *proc = current_thread->process;
	for(int i=0;i<MAX_FD;i++) {
		struct file *exp = NULL;
		if(atomic_compare_exchange_strong(&proc->files[i].file, &exp, file)) {
			kobj_getref(file);
			return i;
		}
	}
	return -1;
}

void process_release_fd(int fd)
{
	struct file *file = atomic_exchange(&current_thread->process->files[fd].file, NULL);
	kobj_putref(file);
}

struct file *process_get_file(int fd)
{
	/* TODO: we need to inc the ref count, and lock */
	return current_thread->process->files[fd].file;
}

ssize_t file_read(struct file *f, size_t off, size_t len, char *buf)
{
	struct inode *ino = file_get_inode(f);
	if(!ino)
		return -1;

	ssize_t ret = ino->ops->read(f, ino, off, len, buf);
	inode_put(ino);
	return ret;
}

ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf)
{
	struct inode *ino = file_get_inode(f);
	if(!ino)
		return -1;

	ssize_t ret = ino->ops->write(f, ino, off, len, buf);
	inode_put(ino);
	return ret;
}

