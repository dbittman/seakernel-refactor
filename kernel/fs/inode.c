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

static bool _inode_page_initialize(void *obj, void *_id, void *data)
{
	int id = *(int *)_id;
	struct inodepage *page = obj;
	page->page = id;
	page->node = data;

	ssize_t thispagelen = page->node->length - id * arch_mm_page_size(0);
	if(thispagelen < 0) thispagelen = 0;
	if((size_t)thispagelen > arch_mm_page_size(0)) thispagelen = arch_mm_page_size(0);
	page->frame = frame_allocate();
	assert(page->node->fs != NULL);
	if(thispagelen && page->node->fs->driver->inode_ops->read_page(page->node, id, page->frame) < 0) {
		kobj_lru_mark_error(&page->node->pages, obj, &page->page);
		return false;
	} else {
		memset((void *)(page->frame + PHYS_MAP_START + thispagelen), 0, arch_mm_page_size(0) - thispagelen);
		kobj_lru_mark_ready(&page->node->pages, obj, &page->page);
		return true;
	}
}

static void _inode_page_release(void *obj, void *data)
{
	(void)data;
	struct inodepage *page = obj;
	/* TODO: should we write back pages in a more lazy way? (eg, during page_init?) */
	if((page->flags & INODEPAGE_DIRTY) && page->node)
		page->node->fs->driver->inode_ops->write_page(page->node, page->page, page->frame);
	frame_release(page->frame);
}

static void _inode_create(void *obj)
{
	struct inode *node = obj;
	kobj_lru_create(&node->pages, sizeof(int), 0, &kobj_inode_page, _inode_page_initialize, _inode_page_release, obj);
	mutex_create(&node->lock);
}

static void _inode_destroy(void *obj)
{
	struct inode *node = obj;
	kobj_lru_destroy(&node->pages);
}

static void _inode_put(void *obj)
{
	struct inode *node = obj;
	(void)node;
	if(node->links == 0 && node->fs) {
		mutex_acquire(&node->fs->lock);
		node->fs->driver->fs_ops->release_inode(node->fs, node);
		mutex_release(&node->fs->lock);
	}
}

static struct kobj kobj_inode = {
	KOBJ_DEFAULT_ELEM(inode),
	.init = NULL,
	.create = _inode_create,
	.put = _inode_put,
	.destroy = _inode_destroy,
};

static ssize_t _inode_proc_lru_read_entry(void *item, size_t off, size_t len, char *buf)
{
	size_t current = 0;
	struct inode *node = item;
	PROCFS_PRINTF(off, len, buf, current,
			"%ld.%ld %c (%d / %d pages)", node->id.fsid, node->id.inoid,
			node->flags & INODE_FLAG_DIRTY ? 'D' : ' ', node->pages.hash.count, (node->length - 1) / arch_mm_page_size(0) + 1);
	return current;
}

static struct kobj_lru inode_lru;
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
	node->mount = NULL;
	assert(node->pages.hash.count == 0);
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
	kobj_lru_release_all(&node->pages);
	if(node->flags & INODE_FLAG_DIRTY)
		fs_update_inode(node);
	if(node->fs) {
		kobj_putref(node->fs);
		node->fs = NULL;
	}
}

__initializer static void _inode_init_lru(void)
{
	kobj_lru_create(&inode_lru, sizeof(struct inode_id), 0, &kobj_inode, _inode_initialize, _inode_release, NULL);
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
	assert(node->_header._koh_refs > 0);
	assert(node->_header._koh_initialized);
	return kobj_lru_get(&node->pages, &nodepage);
}

void inode_release_page(struct inode *node, struct inodepage *page)
{
	if(node->fs)
		kobj_lru_put(&node->pages, page);
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
