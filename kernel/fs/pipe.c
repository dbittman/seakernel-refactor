#include <device.h>
#include <fs/inode.h>
#include <file.h>
#include <charbuffer.h>
#include <fs/sys.h>
#include <errno.h>
#include <fs/stat.h>
#include <string.h>
#include <signal.h>
#include <printk.h>
#include <thread.h>
struct pipe {
	struct kobj_header _header;
	struct charbuffer buf;
	_Atomic int readers, writers;
};

static void _pipe_init(void *obj)
{
	struct pipe *pipe = obj;
	charbuffer_reset(&pipe->buf);
	pipe->readers = pipe->writers = 0;
}

static void _pipe_create(void *obj)
{
	struct pipe *pipe = obj;
	charbuffer_create(&pipe->buf, 0x1000);
}

static void _pipe_destroy(void *obj)
{
	struct pipe *pipe = obj;
	assert(pipe->readers == 0);
	assert(pipe->writers == 0);
	charbuffer_destroy(&pipe->buf);
}

static struct kobj kobj_pipe = {
	KOBJ_DEFAULT_ELEM(pipe), .put = NULL, .destroy = _pipe_destroy,
	.create = _pipe_create, .init = _pipe_init,
};

static void _pipe_file_create(struct file *file)
{
	struct pipe *pipe = file->devdata = kobj_allocate(&kobj_pipe);
	pipe->readers = pipe->writers = 0;
}

static void _pipe_file_destroy(struct file *file)
{
	(void)file;
	kobj_putref(file->devdata);
}

static ssize_t _pipe_read(struct file *file,
		size_t off, size_t len, char *buf)
{
	(void)off;
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
	struct pipe *pipe = file->devdata;
	if(pipe->readers == 0) {
		thread_send_signal(current_thread, SIGPIPE);
		return -EPIPE;
	}

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
	assert(pipe->writers >= 0);
	assert(pipe->readers >= 0);

	if(pipe->writers == 0 || pipe->readers == 0)
		charbuffer_terminate(&pipe->buf); //TODO : named pipes?
}

static int _pipe_select(struct file *file, int flags, struct blockpoint *bp)
{
	int ret = 0;
	struct pipe *pipe = file->devdata;
	
	switch(flags) {
		case SEL_READ:
			if(bp)
				blockpoint_startblock(&pipe->buf.wait_read, bp);
			if(!charbuffer_pending(&pipe->buf)) {
				ret = 1;
			}
			break;
		case SEL_WRITE:
			if(bp)
				blockpoint_startblock(&pipe->buf.wait_write, bp);
			if(!charbuffer_avail(&pipe->buf)) {
				ret = 1;
			}
			break;
		case SEL_ERROR:
			return -1;
	}
	return ret;
}

struct file_calls pipe_fops = {
	.write = _pipe_write,
	.read = _pipe_read,
	.create = _pipe_file_create,
	.destroy = _pipe_file_destroy,
	.open = _pipe_open,
	.close = _pipe_close,
	.map = 0, .unmap = 0,
	.ioctl = 0, .select = _pipe_select,
};

sysret_t sys_pipe(int *fds)
{
	struct file *rf = file_create(NULL, FDT_FIFO);
	struct file *wf = file_create(NULL, 0);
	wf->ops = rf->ops;
	wf->devdata = kobj_getref(rf->devdata);

	wf->flags = F_WRITE;
	rf->flags = F_READ;

	int wfd = process_allocate_fd(wf, 0);
	kobj_putref(wf);
	if(wfd < 0) {
		kobj_putref(rf);
		return -EMFILE;
	}
	process_create_proc_fd(current_thread->process, wfd, "[pipe:write]");
	int rfd = process_allocate_fd(rf, 0);
	kobj_putref(rf);
	if(rfd < 0) {
		process_release_fd(wfd);
		return -EMFILE;
	}
	process_create_proc_fd(current_thread->process, rfd, "[pipe:read]");

	fds[0] = rfd;
	fds[1] = wfd;

	struct pipe *pipe = wf->devdata;
	assert(pipe->readers == 1);
	assert(pipe->writers == 1);

	return 0;
}

