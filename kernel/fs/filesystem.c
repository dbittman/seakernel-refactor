#include <fs/filesystem.h>
#include <fs/inode.h>
#include <system.h>
#include <lib/hash.h>
#include <string.h>
#include <errno.h>
#include <file.h>
#include <printk.h>
static struct kobj_idmap active_filesystems;

static struct hash drivers;
static struct mutex drivers_mutex;

__initializer static void _init_fs(void)
{
	kobj_idmap_create(&active_filesystems, sizeof(uint64_t));
	hash_create(&drivers, HASH_LOCKLESS, 32);
	mutex_create(&drivers_mutex);
}

static _Atomic uint64_t __next_id = 0;

static void _filesystem_init(void *obj)
{
	struct filesystem *fs = obj;
	fs->driver = NULL;
	fs->id = ++__next_id;
	kobj_idmap_insert(&active_filesystems, fs, &fs->id);
}

static void _filesystem_create(void *obj)
{
	struct filesystem *fs = obj;
	mutex_create(&fs->lock);
	_filesystem_init(obj);
}

/* TODO: mounting, unmounting */
static void _filesystem_put(void *obj)
{
	(void)obj;
}

static void _filesystem_destroy(void *obj)
{
	(void)obj;
}

struct kobj kobj_filesystem = {
	.initialized = false,
	.size = sizeof(struct filesystem),
	.name = "filesystem",
	.create = _filesystem_create,
	.init = _filesystem_init,
	.put = _filesystem_put,
	.destroy = _filesystem_destroy,
};

#include <frame.h>
static uintptr_t _fs_inode_map(struct file *file, struct map_region *map, ptrdiff_t d)
{
	struct inode *node = file_get_inode(file);
	if(!node)
		return 0;
	struct inodepage *page = inode_get_page(node, map->nodepage + d / arch_mm_page_size(0));
	inode_put(node);
	frame_acquire(page->frame);
	return page->frame;
}

static void _fs_inode_unmap(struct file *file, struct map_region *map, ptrdiff_t d, uintptr_t phys)
{
	struct frame *frame = frame_get_from_address(phys);
	int persist = frame->flags & FRAME_PERSIST;
	int dirty = frame->flags & FRAME_DIRTY;
	if(frame_release(phys) == 0 && persist) {
		struct inode *node = file_get_inode(file);
		if(node) {
			struct inodepage *page = inode_get_page(node, map->nodepage + d / arch_mm_page_size(0));
			if(dirty)
				page->flags |= INODEPAGE_DIRTY;
			/* TODO: fix hacky way to release this */
			inode_release_page(page);
			inode_release_page(page);
			inode_put(node);
		}
	}
}

static bool _fs_inode_poll(struct file *file, struct pollpoint *point)
{
	/* TODO: support errors */
	point->events &= POLLIN | POLLOUT;
	struct inode *inode = file_get_inode(file);
	if(!inode) {
		*point->revents = POLLERR;
		point->events = 0;
		return true;
	}
	bool ready = false;
	if(point->events & POLLIN) {
		blockpoint_startblock(&inode->readbl, &point->bps[POLL_BLOCK_READ]);
		if(file->pos < inode->length) {
			*point->revents |= POLLIN;
			ready = true;
		}
	}
	if(point->events & POLLOUT) {
		point->events &= ~POLLOUT;
		*point->revents |= POLLOUT;

		ready = true;
	}
	inode_put(inode);
	return ready;
}

struct file_calls fs_fops = {
	.read = inode_read_data,
	.write = inode_write_data,
	.create = 0, .destroy = 0, .ioctl = 0, .open = 0, .close = 0,
	.map = _fs_inode_map,
	.unmap = _fs_inode_unmap,
	.poll = _fs_inode_poll,
};

int fs_mount(struct inode *point, struct filesystem *fs)
{
	struct filesystem *exp = NULL;
	fs->up_mount.fsid = point->id.fsid;
	fs->up_mount.inoid = point->id.inoid;
	bool succ = atomic_compare_exchange_strong(&point->mount, &exp, fs);
	if(!succ)
		return -EBUSY;
	
	kobj_getref(point);
	return 0;
}

void fs_unload_filesystem(struct filesystem *fs)
{
	fs->driver->fs_ops->unmount(fs);
	kobj_putref(fs);
}

struct filesystem *fs_load_filesystem(struct blockdev *bd, const char *type, unsigned long flags, int *err)
{
	if(type) {
		mutex_acquire(&drivers_mutex);
		struct fsdriver *driver = hash_lookup(&drivers, type, strlen(type));
		mutex_release(&drivers_mutex);
		if(!driver) {
			*err = -ENODEV;
			return NULL;
		}
		struct filesystem *fs = kobj_allocate(&kobj_filesystem);
		fs->driver = driver;
		if((*err = driver->fs_ops->mount(fs, bd, flags) < 0)) {
			kobj_putref(fs);
			return NULL;
		}
		return fs;
	} else {
		struct filesystem *fs = kobj_allocate(&kobj_filesystem);
		mutex_acquire(&drivers_mutex);
		struct hashiter iter;
		for(hash_iter_init(&iter, &drivers);
				!hash_iter_done(&iter); hash_iter_next(&iter)) {
			struct fsdriver *driver = hash_iter_get(&iter);

			fs->driver = driver;
			int e = driver->fs_ops->mount(fs, bd, flags);
			if(e == 0) {
				*err = 0;
				mutex_release(&drivers_mutex);
				return fs;
			}
		}
		mutex_release(&drivers_mutex);
		*err = -ENODEV;
		kobj_putref(fs);
		return NULL;
	}
}

int fs_load_inode(uint64_t fsid, uint64_t inoid, struct inode *node)
{
	if(fsid == 0)
		return 0;
	struct filesystem *fs = kobj_idmap_lookup(&active_filesystems, &fsid);
	if(!fs) {
		return -EIO;
	}
	node->fs = fs;
	int ret = fs->driver->fs_ops->load_inode(fs, inoid, node);
	return ret;
}

void fs_update_inode(struct inode *node)
{
	node->fs->driver->fs_ops->update_inode(node->fs, node);
}

int filesystem_register(struct fsdriver *driver)
{
	mutex_acquire(&drivers_mutex);
	int r = hash_insert(&drivers, driver->name, strlen(driver->name), &driver->elem, driver);
	mutex_release(&drivers_mutex);
	return r;
}

void filesystem_deregister(struct fsdriver *driver)
{
	mutex_acquire(&drivers_mutex);
	hash_delete(&drivers, driver->name, strlen(driver->name));
	mutex_release(&drivers_mutex);
}

