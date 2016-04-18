#include <device.h>
#include <file.h>
#include <fs/inode.h>
#include <system.h>
#include <fs/stat.h>
static struct device dev;

char serial_getc(void);
void serial_putc(char);

static ssize_t _serial_read(struct file *f,
		size_t off, size_t len, char *buf)
{
	(void)off;
	(void)f;
	/* TODO: roll out nonblock to everything */
	for(size_t i = 0;i<len;i++)
		*buf++ = serial_getc();
	return len;
}

static ssize_t _serial_write(struct file *f,
		size_t off, size_t len, const char *buf)
{
	(void)off;
	(void)f;
	/* TODO: roll out nonblock to everything */
	for(size_t i = 0;i<len;i++)
		serial_putc(*buf++);
	return len;
}

static struct file_calls serial_calls = {
	.read = _serial_read,
	.write = _serial_write,

	.create = 0, .destroy = 0, .ioctl = 0, .select = 0, .open = 0, .close = 0,
	.map = 0, .unmap = 0,
};

#include <fs/sys.h>
static void _late_init(void)
{
	int ret = sys_mknod("/dev/com0", S_IFCHR | 0666, makedev(dev.devnr, 0));
	assert(ret == 0);
}

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER))
static void _serial_init(void)
{
	init_register_late_call(&_late_init, NULL);
	dev_register(&dev, &serial_calls, S_IFCHR);
}

