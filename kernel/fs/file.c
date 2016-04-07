#include <file.h>
#include <process.h>

struct kobj kobj_file = {
	.name = "file",
	.size = sizeof(struct file),
	.initialized = false,
	.init = NULL,
	.create = NULL,
	.put = NULL,
	.destroy = NULL,
};

