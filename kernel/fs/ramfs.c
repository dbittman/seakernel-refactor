#include <fs/filesystem.h>
#include <slab.h>
#include <system.h>
#include <thread.h>
#include <process.h>
#include <frame.h>
#include <assert.h>
#include <fs/inode.h>
#include <string.h>

struct ramfs_inode {
	struct hash data;
};

struct ramfs_data {
	struct hash inodes;
};

static bool _read_page(struct inode *node, int pagenumber, uintptr_t phys)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	assert(ri != NULL);

	struct frame *frame = hash_lookup(&ri->data, &pagenumber, sizeof(int));
	memcpy((void *)(phys + PHYS_MAP_START), (void *)(frame_get_physical(frame) + PHYS_MAP_START), arch_mm_page_size(0));
	return true;
}

static struct inode_ops ramfs_inode_ops = {
	.read_page = _read_page,
	.write_page = NULL,
	.sync = NULL,
	.update = NULL,
};

static struct fs_ops ramfs_fs_ops = {
	.load_inode = NULL,
};

struct fsdriver ramfs = {
	.inode_ops = &ramfs_inode_ops,
	.fs_ops = &ramfs_fs_ops,
	.name = "ramfs",
	.rootid = 0,
};

static void _ramfs_create(void *obj)
{
	struct ramfs_data *ramfs_data = obj;
	hash_create(&ramfs_data->inodes, 0, 256);
}

struct kobj kobj_ramfs = {
	.initialized = false,
	.name = "ramfs",
	.size = sizeof(struct ramfs_data),
	.create = _ramfs_create,
	.destroy = NULL,
	.init = NULL,
	.put = NULL,
};

static struct ramfs *initial_ramfs;

__orderedinitializer(__orderedafter(FILESYSTEM_INIT_ORDER))
static void _init_ramfs(void)
{
	initial_ramfs = kobj_allocate(&kobj_ramfs);
}

void initial_rootfs_init(void)
{
	current_thread->process->root = kobj_allocate(&kobj_filesystem);
	current_thread->process->root->fsdata = initial_ramfs;
}

