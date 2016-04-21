#include <device.h>
#include <system.h>
#include <file.h>
#include <frame.h>
#include <map.h>
#include <fs/sys.h>
#include <printk.h>
#include <fs/inode.h>
#include <x86_64-ioport.h>
#include <errno.h>
static struct device dev;

static bool _vga_map(struct file *file, struct mapping *map)
{
	(void)file;
	frame_acquire(0xB8000);
	struct inodepage *page = kobj_allocate(&kobj_inode_page);
	page->node = NULL;
	map->page = page;
	page->frame = 0xB8000;
	return true;
}

static void setcurs(int x, int y)
{
	volatile unsigned short position = (y * 80 + x);
	x86_64_outb(0x3D4, 0x0F);
	x86_64_outb(0x3D5, (unsigned char)(position&0xFF));
	x86_64_outb(0x3D4, 0x0E);
	x86_64_outb(0x3D5, (unsigned char )((position>>8)&0xFF));
}


static int _vga_ioctl(struct file *file, long cmd, long arg)
{
	(void)file;
	if(cmd == 0) //set cursor
	{
		setcurs(arg & 0xFFFFFFFF, (arg >> 32) & 0xFFFFFFFF);
		return 0;
	}
	return -EINVAL;
}

static struct file_calls vga_calls = {
	.read = 0,
	.write = 0,
	.create = 0, .destroy = 0, .ioctl = _vga_ioctl, .select = 0, .open = 0, .close = 0,
	.map = _vga_map, .unmap = 0,
};

static void _late_init(void)
{
	int ret = sys_mknod("/dev/vga", S_IFCHR | 0600, makedev(dev.devnr, 0));
	assert(ret == 0);
}

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER))
static void _serial_init(void)
{
	dev_register(&dev, &vga_calls, S_IFCHR);
	init_register_late_call(&_late_init, NULL);
}

