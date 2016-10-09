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
#include <sys.h>
struct pipe {
	struct kobj_header _header;
	struct charbuffer buf;
	struct blocklist statusbl;
	_Atomic int readers, writers;
	bool named;
};

static void _pipe_init(void *obj)
{
	struct pipe *pipe = obj;
	charbuffer_reset(&pipe->buf);
	pipe->readers = pipe->writers = 0;
	pipe->named = false;
}

static void _pipe_create(void *obj)
{
	struct pipe *pipe = obj;
	blocklist_create(&pipe->statusbl);
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
	struct pipe *pipe = kobj_allocate(&kobj_pipe);
	pipe->named = file->dirent != NULL;
	if(pipe->named) {
		struct inode *node = file_get_inode(file);
		pipe->readers = pipe->writers = 0;
		struct pipe *exp = NULL;
		if(!atomic_compare_exchange_strong(&node->pipe, &exp, kobj_getref(pipe))) {
			kobj_putref(pipe);
			kobj_putref(pipe);

			pipe = kobj_getref(exp);
		}
	}
	file->devdata = pipe;
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
	if(pipe->writers == 0 && charbuffer_pending(&pipe->buf) == 0 && !pipe->named) {
		return 0;
	}
	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	ssize_t ret = charbuffer_read(&pipe->buf, buf, len, flags);
	return ret;
}

static ssize_t _pipe_write(struct file *file,
		size_t off, size_t len, const char *buf)
{
	(void)off;
	struct pipe *pipe = file->devdata;
	if(pipe->readers == 0 && !pipe->named) {
		thread_send_signal(current_thread, SIGPIPE);
		return -EPIPE;
	}

	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	ssize_t ret = charbuffer_write(&pipe->buf, buf, len, flags);
	return ret;
}

static void _pipe_open(struct file *file)
{
	struct pipe *pipe = file->devdata;
	if(file->flags & F_WRITE) {
		assert(!(file->flags & F_READ));
		pipe->writers++;
	} else if(file->flags & F_READ) {
		assert(!(file->flags & F_WRITE));
		pipe->readers++;
	}
	pipe->buf.eof = 0;
}

static void _pipe_close(struct file *file)
{
	struct pipe *pipe = file->devdata;
	if(file->flags & F_WRITE) {
		assert(!(file->flags & F_READ));
		pipe->writers--;
	} else if(file->flags & F_READ) {
		assert(!(file->flags & F_WRITE));
		pipe->readers--;
	}
	assert(pipe->writers >= 0);
	assert(pipe->readers >= 0);

	if((pipe->writers == 0 || pipe->readers == 0) && !pipe->named) {
		charbuffer_terminate(&pipe->buf);
		blocklist_unblock_all(&pipe->statusbl);
	} else if(pipe->writers == 0) {
		pipe->buf.eof = 1;
		blocklist_unblock_all(&pipe->buf.wait_read);
		blocklist_unblock_all(&pipe->statusbl);
	}
}

static bool _pipe_poll(struct file *file, struct pollpoint *point)
{
	bool ready = false;
	struct pipe *pipe = file->devdata;
	
	point->events &= POLLIN | POLLOUT;
	point->events |= 1 << POLL_BLOCK_STATUS;

	if(point->events & POLLIN) {
		blockpoint_startblock(&pipe->buf.wait_read, &point->bps[POLL_BLOCK_READ]);
		if(charbuffer_pending(&pipe->buf) > 0 || pipe->buf.eof) {
			*point->revents |= POLLIN;
			ready = true;
		}
	}

	if(point->events & POLLOUT) {
		blockpoint_startblock(&pipe->buf.wait_write, &point->bps[POLL_BLOCK_WRITE]);
		if(charbuffer_avail(&pipe->buf) > 0) {
			*point->revents |= POLLOUT;
			ready = true;
		}
	}

	blockpoint_startblock(&pipe->statusbl, &point->bps[POLL_BLOCK_STATUS]);
	if((pipe->writers == 0 || pipe->readers == 0) && !pipe->named) {
		*point->revents |= POLLHUP;
		ready = true;
	}

	return ready;
}

struct file_calls pipe_fops = {
	.write = _pipe_write,
	.read = _pipe_read,
	.create = _pipe_file_create,
	.destroy = _pipe_file_destroy,
	.open = _pipe_open,
	.close = _pipe_close,
	.map = 0, .unmap = 0,
	.ioctl = 0,
	.poll = _pipe_poll,
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

