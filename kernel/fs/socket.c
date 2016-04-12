#include <device.h>
#include <fs/inode.h>

struct inode_calls socket_iops = {
	.write = NULL,
	.read = NULL
};
