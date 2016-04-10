#include <slab.h>
#include <fs/inode.h>
#include <system.h>
#include <frame.h>
#include <string.h>
#include <fs/filesystem.h>
static void _inode_page_init(void *obj)
{
	struct inodepage *page = obj;
	page->frame = frame_allocate();
}

static void _inode_page_create(void *obj)
{
	struct inodepage *page = obj;
	_inode_page_init(obj);
	mutex_create(&page->lock);
}

static void _inode_page_put(void *obj)
{
	struct inodepage *page = obj;
	frame_release(page->frame);
}

static struct kobj kobj_inode_page = {
	.name = "inode_page",
	.size = sizeof(struct inodepage),
	.initialized = false,
	.init = _inode_page_init,
	.create = _inode_page_create,
	.put = _inode_page_put,
	.destroy = NULL,
};

static bool _inode_page_initialize(void *obj, void *_id, void *data)
{
	int id = *(int *)_id;
	struct inodepage *page = obj;
	page->page = id;
	page->node = data;

	page->node->fs->driver->inode_ops->read_page(page->node, id, page->frame);
	
	kobj_lru_mark_ready(&page->node->pages, obj, _id);
	return true;
}

static void _inode_create(void *obj)
{
	struct inode *node = obj;
	kobj_lru_create(&node->pages, sizeof(int), 0, &kobj_inode_page, _inode_page_initialize, obj);
}

static void _inode_destroy(void *obj)
{
	(void)obj;
}

static struct kobj kobj_inode = {
	.name = "inode",
	.size = sizeof(struct inode),
	.initialized = false,
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
	if(!fs_load_inode(node->id.fsid, node->id.inoid, node)) {
		kobj_lru_mark_error(&inode_lru, obj, id);
		return false;
	} else {
		kobj_lru_mark_ready(&inode_lru, obj, id);
		return true;
	}
}

__initializer static void _inode_init_lru(void)
{
	kobj_lru_create(&inode_lru, sizeof(struct inode_id), 0, &kobj_inode, _inode_initialize, NULL);
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
	return kobj_lru_get(&node->pages, &nodepage);
}

void inode_release_page(struct inode *node, struct inodepage *page)
{
	kobj_lru_put(&node->pages, page);
}

