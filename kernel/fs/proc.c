#include <slab.h>
#include <file.h>
#include <device.h>
#include <system.h>
#include <errno.h>
#include <fs/sys.h>
#include <printk.h>
#include <fs/path.h>
#include <fs/proc.h>
struct proc_entry {
	struct kobj_header _header;
	uint32_t id;
	void *data;
	ssize_t (*call)(void *, int, size_t, size_t, char *);
};

static struct device dev;
static struct kobj_idmap proc_entry_map;

_Atomic uint32_t _next_id = 0;

static void _init_proc_entry(void *obj)
{
	struct proc_entry *pe = obj;
	pe->id = ++_next_id;
	kobj_idmap_insert(&proc_entry_map, pe, &pe->id);
	pe->data = NULL;
}

static void _create_proc_entry(void *obj)
{
	_init_proc_entry(obj);
}

static struct kobj kobj_proc_entry = {
	KOBJ_DEFAULT_ELEM(proc_entry),
	.create = _create_proc_entry,
	.init = _init_proc_entry,
	.put = NULL, .destroy = NULL,
};

ssize_t proc_read_data(uint32_t id, size_t off, size_t len, char *b)
{
	struct proc_entry *pe = kobj_idmap_lookup(&proc_entry_map, &id);
	if(!pe)
		return -EINVAL;

	ssize_t ret = pe->call(pe->data, 0, off, len, b);
	kobj_putref(pe);
	return ret;
}

static ssize_t _proc_read(struct file *file, size_t off, size_t len, char *b)
{
	struct inode *node = file_get_inode(file);
	if(!node)
		return -EINVAL;
	uint32_t id = node->minor;
	inode_put(node);

	return proc_read_data(id, off, len, b);
}

static struct file_calls proc_ops = {
	.read = _proc_read,
	.write = NULL,
	.poll = NULL,
	.ioctl = NULL,
	.create = NULL,
	.destroy = NULL,
	.map = NULL,
	.unmap = NULL,
	.open = NULL,
	.close = NULL,
};

void proc_create(const char *path, ssize_t (*call)(void *data, int, size_t, size_t, char *), void *data)
{
	struct proc_entry *pe = kobj_allocate(&kobj_proc_entry);
	pe->data = data;
	pe->call = call;

	int r = sys_mknod(path, S_IFCHR | 0600, makedev(dev.devnr, pe->id));
	assert(r == 0);
	kobj_putref(pe);
}

void proc_destroy(const char *path)
{
	struct inode *node;
	if(fs_path_resolve(path, NULL, 0, 0, NULL, &node) < 0)
		return;

	uint32_t id = node->minor;
	inode_put(node);

	struct proc_entry *pe = kobj_idmap_lookup(&proc_entry_map, &id);
	if(pe == NULL)
		return;

	kobj_idmap_delete(&proc_entry_map, pe, &id);
	kobj_putref(pe);
	int r = sys_unlink(path);
	assert(r == 0);
}

static ssize_t _resolve_self(void *data, int read, size_t off, size_t len, char *buf)
{
	(void)data;
	size_t current = 0;
	if(read != 0)
		return -EINVAL;
	PROCFS_PRINTF(off, len, buf, current,
			"%d", current_thread->process->pid);
	return current;
}

static void _late_init(void)
{
	struct proc_entry *pe = kobj_allocate(&kobj_proc_entry);
	pe->call = _resolve_self;

	int r = sys_mknod("/proc/self", S_IFLNK | 0644, makedev(dev.devnr, pe->id));
	assert(r == 0);
}

__orderedinitializer(__orderedafter(DEVICE_INITIALIZER_ORDER)) static void _proc_init(void)
{
	dev_register(&dev, &proc_ops, S_IFCHR);
	init_register_late_call(&_late_init, NULL);
	kobj_idmap_create(&proc_entry_map, sizeof(uint32_t));
}

ssize_t _proc_read_int(void *data, int rw, size_t off, size_t len, char *buf)
{
	if(rw != 0)
		return -EINVAL;
	int value = *(int *)data;
	size_t current = 0;
	PROCFS_PRINTF(off, len, buf, current,
			"%d", value);
	return current;
}

