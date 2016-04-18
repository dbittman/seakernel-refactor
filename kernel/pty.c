#include <device.h>
#include <slab.h>
#include <charbuffer.h>
#include <file.h>
#include <fs/sys.h>
#include <thread.h>
#include <process.h>

static struct device dev;

struct pty {
	struct kobj_header _header;
	struct charbuffer input, output;
};

static void _pty_init(void *obj)
{
	struct pty *pty = obj;
	(void)pty;
}

static void _pty_create(void *obj)
{
	struct pty *pty = obj;
	charbuffer_create(&pty->input, 0x1000);
	charbuffer_create(&pty->output, 0x1000);
	_pty_init(obj);
}

static void _pty_put(void *obj)
{
	struct pty *pty = obj;
	(void)pty;
}

static void _pty_destroy(void *obj)
{
	struct pty *pty = obj;
	(void)pty;
}

struct kobj kobj_pty = {
	KOBJ_DEFAULT_ELEM(pty),
	.create = _pty_create,
	.destroy = _pty_destroy,
	.init = _pty_init,
	.put = _pty_put,
};




static void _pty_fops_open(struct file *file)
{
	if(!(file->flags & O_NOCTTY)) {
		struct pty *old = current_thread->process->pty;
		current_thread->process->pty = kobj_getref(file->devdata);
		kobj_putref(old);
	}
}

static void _pty_fops_create(struct file *file)
{
	file->devdata = kobj_allocate(&kobj_pty);
}

static void _pty_fops_destroy(struct file *file)
{
	kobj_putref(file->devdata);
}

static struct file_calls pty_fops = {
	.create = _pty_fops_create,
	.destroy = _pty_fops_destroy,
	.open = _pty_fops_open,
	.close = NULL,
	.select = NULL,
	.ioctl = NULL,
	.read = NULL,
	.write = NULL,
	.map = NULL, .unmap = NULL,
};

#include <system.h>
__initializer static void _init_pty(void)
{
	dev_register(&dev, &pty_fops, S_IFCHR);
}

