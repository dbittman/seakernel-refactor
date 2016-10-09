#include <device.h>
#include <system.h>
#include <fs/sys.h>
#include <blocklist.h>
#include <x86_64-ioport.h>
#include <charbuffer.h>
#include <file.h>
#include <interrupt.h>
#include <printk.h>
#include <thread.h>
#include <process.h>
static struct device dev;

static struct charbuffer keybuf;

static ssize_t _read(struct file *file, size_t off, size_t len, char *buf)
{
	(void)off;
	(void)file;
	return charbuffer_read(&keybuf, buf, len, ((file->flags & O_NONBLOCK) ? CHARBUFFER_DO_NONBLOCK : 0) | CHARBUFFER_DO_ANY);
}

static bool _poll(struct file *file, struct pollpoint *point)
{
	(void)file;
	point->events &= POLLIN;
	if(point->events & POLLIN) {
		blockpoint_startblock(&keybuf.wait_read, &point->bps[POLL_BLOCK_READ]);
		if(charbuffer_pending(&keybuf)) {
			*point->revents |= POLLIN;
			return true;
		}
	}
	return false;
}

static struct file_calls keyboard_ops = {
	.open = NULL,
	.close = NULL,
	.create = NULL,
	.destroy = NULL,
	.map = NULL,
	.unmap = NULL,
	.ioctl = NULL,
	.read = _read,
	.write = NULL,
	.poll = _poll,
};

void flush_port(void)
{
    unsigned temp;
    do
    {
        temp = x86_64_inb(0x64);
        if((temp & 0x01) != 0)
        {
            (void)x86_64_inb(0x60);
            continue;
        }
    } while((temp & 0x02) != 0);
}

static void _key_interrupt(int v, int flags)
{
	(void)v;
	(void)flags;
	unsigned char tmp;
	do {
		tmp = x86_64_inb(0x64);
		if(tmp & 1) {
			unsigned char scan = x86_64_inb(0x60);
			charbuffer_write(&keybuf, (char *)&scan, 1, CHARBUFFER_DO_NONBLOCK);
		}
	} while(tmp & 2);
}

static void _late_init(void)
{
	sys_mknod("/dev/keyboard", S_IFCHR | 0600, makedev(dev.devnr, 0));
}

#include <processor.h>
__initializer static void _init_keyboard(void)
{
	interrupt_register(33, &_key_interrupt);
	arch_interrupt_unmask(33);
	init_register_late_call(&_late_init, NULL);
	dev_register(&dev, &keyboard_ops, S_IFCHR);
	charbuffer_create(&keybuf, 0x1000);
	flush_port();
}

