#include <slab.h>
#include <fs/inode.h>
#include <system.h>
static void _inode_create(void *obj)
{
	(void)obj;
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

static bool _inode_initialize(void *obj, void *id)
{
	kobj_lru_mark_ready(&inode_lru, obj, id);
	return true;
}

__initializer static void _inode_init_lru(void)
{
	kobj_lru_create(&inode_lru, sizeof(struct inode_id), 0, &kobj_inode, _inode_initialize);
}

void inode_put(struct inode *inode)
{
	kobj_lru_put(&inode_lru, inode);
}

void inode_release_page(struct inode *node, int nodepage)
{
	(void)node;
	(void)nodepage;
}

