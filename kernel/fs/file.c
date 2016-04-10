#include <file.h>
#include <process.h>
#include <thread.h>

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
		if(atomic_compare_exchange_strong(&proc->files[i], &exp, file)) {
			kobj_getref(file);
			return i;
		}
	}
	return -1;
}

void process_release_fd(int fd)
{
	struct file *file = atomic_exchange(&current_thread->process->files[fd], NULL);
	kobj_putref(file);
}

struct file *process_get_file(int fd)
{
	return current_thread->process->files[fd];
}

