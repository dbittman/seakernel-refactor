#include <slab.h>
#include <fs/inode.h>
#include <system.h>
#include <frame.h>
#include <string.h>
#include <fs/filesystem.h>
#include <printk.h>
#include <fs/stat.h>
#include <device.h>
#include <thread.h>
#include <process.h>
#include <fs/proc.h>

struct kobj kobj_inode_page = KOBJ_DEFAULT(inodepage);

static struct kobj_lru inode_lru;
static struct kobj_lru inodepage_lru;

static bool _inode_page_initialize(void *obj, void *_id, void *data)
{
	(void)data;
	struct inodepage *page = obj;
	memcpy(&page->id, _id, sizeof(page->id));

	struct inode_id nodeid = {.fsid = page->id.fsid, .inoid = page->id.inoid };
	struct inode *node = inode_lookup(&nodeid);
	//printk("Loading page for %s:%ld (%ld)\n", node->fs->driver->name, page->id.inoid, page->id.page);

	ssize_t thispagelen = node->length - page->id.page * arch_mm_page_size(0);
	if(thispagelen < 0) thispagelen = 0;
	if((size_t)thispagelen > arch_mm_page_size(0)) thispagelen = arch_mm_page_size(0);
	/* TODO: do we need to reallocate a frame if we're reclaiming an old page? */
	page->frame = frame_allocate(0, FRAME_PERSIST | FRAME_NOCLEAR | FRAME_ZEROCOUNT);
	assert(node->fs != NULL);
	if(thispagelen && node->fs->driver->inode_ops->read_page(node, page->id.page, page->frame) < 0) {
		kobj_lru_mark_error(&inodepage_lru, obj, &page->id);
		inode_put(node);
		return false;
	} else {
		memset((void *)(page->frame + PHYS_MAP_START + thispagelen), 0, arch_mm_page_size(0) - thispagelen);
		page->node = node;
		kobj_lru_mark_ready(&inodepage_lru, obj, &page->id);
		return true;
	}
}

static void _inode_page_release(void *obj, void *data)
{
	(void)data;
	struct inodepage *page = obj;

	/* TODO: should we write back pages in a more lazy way? (eg, during page_init?) */
	if(page->flags & INODEPAGE_DIRTY)
		page->node->fs->driver->inode_ops->write_page(page->node, page->id.page, page->frame);
	assert(frame_get_from_address(page->frame)->count == 0);
	mm_physical_deallocate(page->frame);
	inode_put(page->node);
	page->node = NULL;
}

static void _inode_create(void *obj)
{
	struct inode *node = obj;
	mutex_create(&node->lock);
}

static struct kobj kobj_inode = {
	KOBJ_DEFAULT_ELEM(inode),
	.init = NULL,
	.create = _inode_create,
	.put = NULL,
	.destroy = NULL,
};

static ssize_t _inode_proc_lru_read_entry(void *item, size_t off, size_t len, char *buf)
{
	size_t current = 0;
	struct inode *node = item;
	PROCFS_PRINTF(off, len, buf, current,
			"%ld.%ld %c", node->id.fsid, node->id.inoid,
			(node->flags & INODE_FLAG_DIRTY) ? 'D' : ' ');
	return current;
}

static struct kobj_lru_proc_info _proc_inode_lru_info = {
	.lru = &inode_lru,
	.options = 0,
	.read_entry = _inode_proc_lru_read_entry,
};

static void _late_init(void)
{
	proc_create("/proc/inodes", kobj_lru_proc_read, &_proc_inode_lru_info);
}
LATE_INIT_CALL(_late_init, NULL);

static bool _inode_initialize(void *obj, void *id, void *data)
{
	(void)data;
	struct inode *node = obj;
	node->flags = 0;
	node->mount = NULL;
	node->fs = NULL;
	node->major = node->minor = 0;
	memcpy(&node->id, id, sizeof(node->id));
	if(fs_load_inode(node->id.fsid, node->id.inoid, node) < 0) {
		kobj_lru_mark_error(&inode_lru, obj, &node->id);
		return false;
	} else {
		kobj_lru_mark_ready(&inode_lru, obj, &node->id);
		return true;
	}
}

static void _inode_release(void *obj, void *data)
{
	(void)data;
	struct inode *node = obj;
	if((node->flags & INODE_FLAG_DIRTY) && node->links && node->fs)
		fs_update_inode(node);
	if(node->links == 0 && node->fs && !(atomic_fetch_or(&node->flags, INODE_FLAG_UNLINKED) & INODE_FLAG_UNLINKED)) {
		mutex_acquire(&node->fs->lock);
		node->fs->driver->fs_ops->release_inode(node->fs, node);
		mutex_release(&node->fs->lock);
	}
	
	if(node->fs) {
		kobj_putref(node->fs);
	}
}

__initializer static void _inode_init_lru(void)
{
	/* TODO: sane defaults for these maximums? */
	kobj_lru_create(&inode_lru, sizeof(struct inode_id), 1000, &kobj_inode, _inode_initialize, _inode_release, NULL, NULL);
	kobj_lru_create(&inodepage_lru, sizeof(struct inodepage_id), 10000, &kobj_inode_page, _inode_page_initialize, _inode_page_release, NULL, NULL);
}

struct inode *inode_lookup(struct inode_id *id)
{
	return kobj_lru_get(&inode_lru, id);
}

void inode_put(struct inode *inode)
{
	kobj_lru_put(&inode_lru, inode);
}

struct inodepage *inode_get_page(struct inode *node, int nodepage)
{
	if(!node->fs)
		return NULL;
	
	struct inodepage_id id = {.fsid = node->fs->id, .inoid = node->id.inoid, .page = nodepage };
	return kobj_lru_get(&inodepage_lru, &id);
}

void inode_release_page(struct inodepage *page)
{
	kobj_lru_put(&inodepage_lru, page);
}

static bool _do_inode_check_perm(struct inode *node, int type, int uid, int gid)
{
	if(uid == 0)
		return true;
	if((uid == node->uid)
			&& (type & node->mode)) {
		return true;
	}
	type >>= 3;
	if(gid == node->gid
			&& (type & node->mode)) {
		return true;
	}
	type >>= 3;
	if(type & node->mode) {
		return true;
	}
	return false;
}

bool inode_check_perm(struct inode *node, int type)
{
	return _do_inode_check_perm(node, type, current_thread->process->euid, current_thread->process->egid);
}

bool inode_check_access(struct inode *node, int type)
{
	return _do_inode_check_perm(node, type, current_thread->process->uid, current_thread->process->gid);
}
