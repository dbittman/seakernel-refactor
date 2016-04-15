#include <slab.h>
#include <fs/inode.h>
#include <system.h>
#include <frame.h>
#include <string.h>
#include <fs/filesystem.h>
#include <printk.h>
#include <fs/stat.h>
#include <device.h>

static struct kobj kobj_inode_page = KOBJ_DEFAULT(inodepage);

static bool _inode_page_initialize(void *obj, void *_id, void *data)
{
	int id = *(int *)_id;
	struct inodepage *page = obj;
	page->page = id;
	page->node = data;

	page->frame = frame_allocate();
	assert(page->node->fs != NULL);
	if(page->node->fs->driver->inode_ops->read_page(page->node, id, page->frame) < 0) {
		kobj_lru_mark_error(&page->node->pages, obj, &page->page);
		return false;
	} else {
		kobj_lru_mark_ready(&page->node->pages, obj, &page->page);
		return true;
	}
}

static void _inode_page_release(void *obj, void *data)
{
	(void)data;
	struct inodepage *page = obj;
	/* TODO: should we write back pages in a more lazy way? (eg, during page_init?) */
	if(page->flags & INODEPAGE_DIRTY)
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

static struct kobj kobj_inode = {
	KOBJ_DEFAULT_ELEM(inode),
	.init = NULL,
	.create = _inode_create,
	.put = NULL,
	.destroy = _inode_destroy,
};

static struct kobj_lru inode_lru;

static bool _inode_initialize(void *obj, void *id, void *data)
{
	(void)data;
	struct inode *node = obj;
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
	return kobj_lru_get(&node->pages, &nodepage);
}

void inode_release_page(struct inode *node, struct inodepage *page)
{
	if(node->fs)
		kobj_lru_put(&node->pages, page);
}

