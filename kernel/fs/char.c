#include <device.h>
#include <fs/inode.h>
#include <string.h>
#include <fs/stat.h>
#include <system.h>
#include <file.h>
/* default char devices */

static struct device dev;

static ssize_t _char_read(struct file *file, size_t off, size_t len, char *b)
{
	(void)file;
	(void)off;
	struct inode *node = file_get_inode(file);
	if(node->minor == 1)
		memset(b, 0, len);

	return 0;
}

static ssize_t _char_write(struct file *file, size_t off, size_t len, const char *b)
{
	(void)b;
	(void)len;
	(void)off;
	(void)file;

	return 0;
}

static struct file_calls char_calls = {
	.read = _char_read,
	.write = _char_write,
	.create = 0, .destroy = 0, .ioctl = 0, .select = 0, .open = 0, .close = 0,
};

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER)) static void _init_char(void)
{
	dev_register(&dev, &char_calls, S_IFCHR);
}

int dev_char_builtin_major(void)
{
	return dev.devnr;
}

