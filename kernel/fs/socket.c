#include <device.h>
#include <file.h>

struct file_calls socket_fops = {
	.write = NULL,
	.read = NULL,
	.create = 0, .destroy = 0, .ioctl = 0, .select = 0, .open = 0, .close = 0,
};
