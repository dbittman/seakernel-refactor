#include <device.h>
#include <fs/inode.h>
#include <file.h>
#include <charbuffer.h>
#include <fs/sys.h>
#include <errno.h>
#include <fs/stat.h>
#include <string.h>
#include <printk.h>
struct pipe {
	struct kobj_header _header;
	struct charbuffer buf;
	_Atomic int readers, writers;
};

static struct kobj kobj_pipe = KOBJ_DEFAULT(pipe);

static void _pipe_create(struct file *file)
{
	struct pipe *pipe = file->devdata = kobj_allocate(&kobj_pipe);
	charbuffer_create(&pipe->buf, 0x1000);
	pipe->readers = pipe->writers = 1;
}

static void _pipe_destroy(struct file *file)
{
	kobj_putref(file->devdata);
}

static ssize_t _pipe_read(struct file *file,
		size_t off, size_t len, char *buf)
{
	(void)off;
	printk("PIPE READ\n");
	struct pipe *pipe = file->devdata;
	if(pipe->writers == 0 && charbuffer_pending(&pipe->buf) == 0)
		return 0;
	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	size_t ret = charbuffer_read(&pipe->buf, buf, len, flags);
	return ret;
}

static ssize_t _pipe_write(struct file *file,
		size_t off, size_t len, const char *buf)
{
	(void)off;
	printk("PIPE WRITE\n");
	struct pipe *pipe = file->devdata;
	if(pipe->readers == 0)
		return -EPIPE; //TODO: also raise SIGPIPE.

	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	size_t ret = charbuffer_write(&pipe->buf, buf, len, flags);

	return ret;
}

static void _pipe_open(struct file *file)
{
	struct pipe *pipe = file->devdata;
	if(file->flags & F_WRITE)
		pipe->writers++;
	if(file->flags & F_READ)
		pipe->readers++;
}

static void _pipe_close(struct file *file)
{
	struct pipe *pipe = file->devdata;
	if(file->flags & F_WRITE)
		pipe->writers--;
	if(file->flags & F_READ)
		pipe->readers--;
	/* TODO: notify charbuffer of closure */
}

struct file_calls pipe_fops = {
	.write = _pipe_write,
	.read = _pipe_read,
	.create = _pipe_create,
	.destroy = _pipe_destroy,
	.open = _pipe_open,
	.close = _pipe_close,

	.ioctl = 0, .select = 0,
};

int sys_pipe(int *fds)
{
	struct file *rf = file_create(NULL, FDT_FIFO);
	struct file *wf = file_create(NULL, 0);
	wf->ops = rf->ops;
	wf->devdata = rf->devdata;

	wf->flags = F_WRITE;
	rf->flags = F_READ;

	int wfd = process_allocate_fd(wf);
	if(wfd < 0) {
		kobj_putref(rf);
		kobj_putref(wf);
		return -EMFILE;
	}
	int rfd = process_allocate_fd(rf);
	if(rfd < 0) {
		kobj_putref(rf);
		kobj_putref(wf);
		return -EMFILE;
	}

	fds[0] = rfd;
	fds[1] = wfd;

	return 0;
}

