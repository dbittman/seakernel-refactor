#include <device.h>
#include <file.h>
#include <fs/inode.h>
#include <system.h>
#include <fs/stat.h>
static struct device dev;

char serial_getc(void);
void serial_putc(char);

static ssize_t _serial_read(struct file *f, struct inode *node,
		size_t off, size_t len, char *buf)
{
	(void)off;
	(void)node;
	(void)f;
	/* TODO: roll out nonblock to everything */
	for(size_t i = 0;i<len;i++)
		*buf++ = serial_getc();
	return len;
}

static ssize_t _serial_write(struct file *f, struct inode *node,
		size_t off, size_t len, const char *buf)
{
	(void)off;
	(void)node;
	(void)f;
	/* TODO: roll out nonblock to everything */
	for(size_t i = 0;i<len;i++)
		serial_putc(*buf++);
	return len;
}

static struct inode_calls serial_calls = {
	.read = _serial_read,
	.write = _serial_write,
};

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER))
static void _serial_init(void)
{
	dev_register(&dev, &serial_calls, S_IFCHR);
}

int dev_com_builtin_major(void)
{
	return dev.devnr;
}

