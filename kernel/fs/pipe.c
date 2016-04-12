#include <device.h>
#include <fs/inode.h>
#include <file.h>
#include <charbuffer.h>
#include <fs/sys.h>
#include <errno.h>

struct pipe {
	struct kobj_header _header;
	struct charbuffer buf;
	_Atomic int readers, writers;
};

static struct kobj kobj_pipe = KOBJ_DEFAULT(pipe);

static void _pipe_create(struct inode *node)
{
	node->devdata = kobj_allocate(&kobj_pipe);
}

static void _pipe_destroy(struct inode *node)
{
	kobj_putref(node->devdata);
}

static ssize_t _pipe_read(struct file *file, struct inode *node,
		size_t off, size_t len, char *buf)
{
	(void)off;
	struct pipe *pipe = node->devdata;
	if(pipe->writers == 0 && charbuffer_pending(&pipe->buf) == 0)
		return 0;
	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	size_t ret = charbuffer_read(&pipe->buf, buf, len, flags);
	return ret;
}

static ssize_t _pipe_write(struct file *file, struct inode *node,
		size_t off, size_t len, const char *buf)
{
	(void)off;
	struct pipe *pipe = node->devdata;
	if(pipe->readers == 0)
		return -EPIPE; //TODO: also raise SIGPIPE.

	int flags = CHARBUFFER_DO_ANY;
	if(file->flags & O_NONBLOCK)
		flags |= CHARBUFFER_DO_NONBLOCK;
	size_t ret = charbuffer_write(&pipe->buf, buf, len, flags);

	return ret;
}

static void _pipe_open(struct file *file, struct inode *node)
{
	struct pipe *pipe = node->devdata;
	if(file->flags & F_WRITE)
		pipe->writers++;
	if(file->flags & F_READ)
		pipe->readers++;
}

static void _pipe_close(struct file *file, struct inode *node)
{
	struct pipe *pipe = node->devdata;
	if(file->flags & F_WRITE)
		pipe->writers--;
	if(file->flags & F_READ)
		pipe->readers--;
	/* TODO: notify charbuffer of closure */
}

struct inode_calls pipe_iops = {
	.write = _pipe_write,
	.read = _pipe_read,
	.create = _pipe_create,
	.destroy = _pipe_destroy,
	.open = _pipe_open,
	.close = _pipe_close,
};

