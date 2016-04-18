#include <device.h>
#include <slab.h>
#include <charbuffer.h>
#include <file.h>
#include <fs/sys.h>
#include <thread.h>
#include <process.h>
#include <fs/inode.h>
#include <termios.h>
#include <ioctl.h>
#include <errno.h>
#include <printk.h>
static struct device dev;

#define CBUF_SIZE 256

struct pty {
	struct kobj_header _header;
	long id;
	struct charbuffer input, output;

	char cbuf[CBUF_SIZE];
	int cbuf_pos;
	struct mutex cbuf_lock;

	struct winsize size;
	struct termios term;
};

static _Atomic long _next_pty_id = 0;
static struct kobj_idmap pty_idmap;

struct pty_file {
	struct kobj_header _header;
	struct pty *pty;
	bool master;
};

static void _pty_init(void *obj)
{
	struct pty *pty = obj;
	pty->id = ++_next_pty_id;
	kobj_idmap_insert(&pty_idmap, pty, &pty->id);
}

static void _pty_create(void *obj)
{
	struct pty *pty = obj;
	charbuffer_create(&pty->input, 0x1000);
	charbuffer_create(&pty->output, 0x1000);
	mutex_create(&pty->cbuf_lock);
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

static struct kobj kobj_pty = {
	KOBJ_DEFAULT_ELEM(pty),
	.create = _pty_create,
	.destroy = _pty_destroy,
	.init = _pty_init,
	.put = _pty_put,
};

static struct kobj kobj_pty_file = KOBJ_DEFAULT(pty_file);









static size_t pty_read_master(struct pty *pty, char *buffer, size_t length, bool block)
{
	return charbuffer_read(&pty->output, buffer, length, (block ? 0 : CHARBUFFER_DO_NONBLOCK) | CHARBUFFER_DO_ANY);
}

static void write_char(struct pty *pty, char c)
{
	if(c == '\n' && (pty->term.c_oflag & ONLCR)) {
		char d = '\r';
		charbuffer_write(&pty->output, &d, 1, CHARBUFFER_DO_NONBLOCK);
	}
	charbuffer_write(&pty->output, &c, 1, CHARBUFFER_DO_NONBLOCK);
}

static void __raise_action(struct pty *pty, int sig)
{
	(void)pty;
	(void)sig;
	/*__linkedlist_lock(process_list);
	struct linkedentry *node;
	for(node = linkedlist_iter_start(process_list); node != linkedlist_iter_end(process_list);
			node = linkedlist_iter_next(node)) {
		struct process *proc = linkedentry_obj(node);
		if(proc->pty == pty) {
			tm_signal_send_process(proc, sig);
		}
	}
	__linkedlist_unlock(process_list);*/
}

static void process_input(struct pty *pty, char c)
{
	if(pty->cbuf_pos < (CBUF_SIZE - 1)) {
		if(c == pty->term.c_cc[VINTR]) {
			__raise_action(pty, SIGINT);
			if(pty->term.c_lflag & ECHO) {
				write_char(pty, '^');
				write_char(pty, 'C');
				write_char(pty, '\n');
			}
		} else if(c == pty->term.c_cc[VERASE]) {
			if(pty->cbuf_pos > 0) {
				pty->cbuf[pty->cbuf_pos--] = 0;
				if(pty->term.c_lflag & ECHO) {
					write_char(pty, '\b');
					write_char(pty, ' ');
					write_char(pty, '\b');
				}
			}
		} else if(c == pty->term.c_cc[VSUSP]) {
			__raise_action(pty, SIGTSTP);
			if(pty->term.c_lflag & ECHO) {
				write_char(pty, '^');
				write_char(pty, 'Z');
				write_char(pty, '\n');
			}
		} else if(c == pty->term.c_cc[VEOF]) {
			if(pty->cbuf_pos > 0) {
				/* TODO: should these be non block? */
				charbuffer_write(&pty->input, pty->cbuf, pty->cbuf_pos, 0);
				pty->cbuf_pos = 0;
			} else {
				pty->input.eof = 1;
				blocklist_unblock_all(&pty->input.wait_read);
			}
		} else {
			if(c == 27) /* escape */
				c = '^';
			pty->cbuf[pty->cbuf_pos++] = c;
			if(pty->term.c_lflag & ECHO)
				write_char(pty, c);
			if(c == '\n') {
				charbuffer_write(&pty->input, pty->cbuf, pty->cbuf_pos, 0);
				pty->cbuf_pos = 0;
			}
		}
	}
}

static size_t pty_write_master(struct pty *pty, const char *buffer, size_t length, bool block)
{
	if(pty->term.c_lflag & ICANON) {
		mutex_acquire(&pty->cbuf_lock);
		for(size_t i = 0;i<length;i++) {
			process_input(pty, *buffer++);
		}
		mutex_release(&pty->cbuf_lock);
		return length;
	} else {
		if(pty->term.c_lflag & ECHO)
			charbuffer_write(&pty->output, buffer, length, block | CHARBUFFER_DO_ANY);
		return charbuffer_write(&pty->input, buffer, length, block | CHARBUFFER_DO_ANY);
	}
}

static size_t pty_read_slave(struct pty *pty, char *buffer, size_t length, bool block)
{
	return charbuffer_read(&pty->input, buffer, length, block);
}

static size_t pty_write_slave(struct pty *pty, const char *buffer, size_t length, bool block)
{
	for(size_t i=0;i<length;i++) {
		if(*buffer == '\n' && (pty->term.c_oflag & ONLCR)) {
			charbuffer_write(&pty->output, (char *)"\r", 1, block | CHARBUFFER_DO_ANY);
		}
		charbuffer_write(&pty->output, buffer++, 1, block | CHARBUFFER_DO_ANY);
	}
	return length;
}

static ssize_t _pty_fops_read(struct file *file, size_t off, size_t len, char *buffer)
{
	if(!file->devdata)
		return -EINVAL;
	(void)off;
	bool block = !(file->flags & O_NONBLOCK);
	struct pty_file *pf = file->devdata;
	if(pf->master)
		return pty_read_master(pf->pty, buffer, len, block);
	else
		return pty_read_slave(pf->pty, buffer, len, block);
}

static ssize_t _pty_fops_write(struct file *file, size_t off, size_t len, const char *buffer)
{
	if(!file->devdata)
		return -EINVAL;
	(void)off;
	bool block = !(file->flags & O_NONBLOCK);
	struct pty_file *pf = file->devdata;
	if(pf->master)
		return pty_write_master(pf->pty, buffer, len, block);
	else
		return pty_write_slave(pf->pty, buffer, len, block);
}












static void _pty_fops_open(struct file *file)
{
	if(!file->devdata)
		return;
	kobj_getref(file->devdata);

	if(!(file->flags & O_NOCTTY)) {
		struct pty_file *old = current_thread->process->pty;
		current_thread->process->pty = kobj_getref(file->devdata);
		kobj_putref(old);
	}
}

static void _pty_fops_close(struct file *file)
{
	if(!file->devdata)
		return;
	kobj_putref(file->devdata);
}

static void _pty_fops_create(struct file *file)
{
	struct inode *node = file_get_inode(file);
	struct pty_file *pf = file->devdata = kobj_allocate(&kobj_pty_file);
	if(node->minor != 0) {
		long id = node->minor;
		pf->pty = kobj_idmap_lookup(&pty_idmap, &id);
		pf->master = false;
	} else {
		pf->master = true;
		struct pty *np = kobj_allocate(&kobj_pty);
		char str[128];
		snprintf(str, 128, "/dev/pts/%d", np->id);
		sys_mknod(str, S_IFCHR | 0666, makedev(dev.devnr, np->id));
		pf->pty = np;
	}
	inode_put(node);
	assert(pf->pty != NULL);
}

static void _pty_fops_destroy(struct file *file)
{
	if(!file->devdata)
		return;
	kobj_putref(file->devdata);
}

/* TODO: check for /dev/tty */
static int _pty_fops_ioctl(struct file *file, long cmd, long arg)
{
	int ret = 0;
	struct pty_file *pf = file->devdata;
	printk("IOCTL: (%lx) (%lx)\n", cmd, (long)TIOCGPTN);
	switch(cmd) {
		case TIOCGPTN:
			printk("IOCTL: %ld %d\n", pf->pty->id, pf->master);
			if(pf->master) {
				*(int *)arg = pf->pty->id;
			} else {
				ret = -EINVAL;
			}
	}
	return ret;
}

static int _pty_fops_select(struct file *file, int flags, struct blockpoint *bp)
{
	struct pty_file *pf = file->devdata;
	struct pty *pty = pf->pty;
	if(flags & SEL_ERROR)
		return -1;
	struct blocklist *bl;
	size_t ready = 0;
	if(pf->master && (flags == SEL_READ)) {
		bl = &pty->output.wait_read;
		ready = charbuffer_pending(&pty->output);
	} else if(pf->master && (flags == SEL_WRITE)) {
		bl = &pty->input.wait_write;
		ready = charbuffer_avail(&pty->input);
	} else if(!pf->master && (flags == SEL_READ)) {
		bl = &pty->input.wait_read;
		ready = charbuffer_pending(&pty->input);
	} else {
		bl = &pty->output.wait_write;
		ready = charbuffer_avail(&pty->output);
	}
	if(bp)
		blockpoint_startblock(bl, bp);
	
	return ready > 0;
}

static struct file_calls pty_fops = {
	.create = _pty_fops_create,
	.destroy = _pty_fops_destroy,
	.open = _pty_fops_open,
	.close = _pty_fops_close,
	.select = _pty_fops_select,
	.ioctl = _pty_fops_ioctl,
	.read = _pty_fops_read,
	.write = _pty_fops_write,
	.map = NULL, .unmap = NULL,
};

#include <system.h>

static void _late_init(void)
{
	int r = sys_mknod("/dev/ptmx", S_IFCHR | 0666, makedev(dev.devnr, 0));
	assert(r == 0);
	r = sys_open("/dev/pts", O_RDWR | O_CREAT, S_IFDIR | 0777);
	sys_close(r);
}

__initializer static void _init_pty(void)
{
	init_register_late_call(&_late_init, NULL);
	dev_register(&dev, &pty_fops, S_IFCHR);
	kobj_idmap_create(&pty_idmap, sizeof(long));
}

