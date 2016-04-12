#include <fs/filesystem.h>
#include <slab.h>
#include <system.h>
#include <thread.h>
#include <process.h>
#include <frame.h>
#include <assert.h>
#include <fs/inode.h>
#include <string.h>
#include <machine/machine.h>
#include <printk.h>
#include <fs/dirent.h>
#include <mutex.h>

struct ramfs_inode {
	struct kobj _header;
	struct hash data;
	struct hash dirents;
	int mode, atime, mtime, ctime;
	struct hashelem elem;
	uint64_t id;
	struct mutex lock;
	_Atomic size_t length;
};

struct ramfs_dirent {
	struct kobj _header;
	char name[256];
	size_t namelen;
	uint64_t ino;
	struct hashelem elem;
};

static void _ramfs_inode_init(void *obj)
{
	struct ramfs_inode *i = obj;
	i->length = 0;
}

static void _ramfs_inode_create(void *obj)
{
	struct ramfs_inode *i = obj;
	hash_create(&i->data, 0, 256);
	hash_create(&i->dirents, 0, 256);
	mutex_create(&i->lock);
	_ramfs_inode_init(obj);
}

static void _ramfs_inode_destroy(void *obj)
{
	struct ramfs_inode *i = obj;
	hash_destroy(&i->data);
	hash_destroy(&i->dirents);
}

struct kobj kobj_ramfs_inode = {
	.initialized = false,
	.size = sizeof(struct ramfs_inode),
	.name = "ramfs_inode",
	.put = NULL, .destroy = _ramfs_inode_destroy,
	.create = _ramfs_inode_create, .init = _ramfs_inode_init,
};

struct kobj kobj_ramfs_dirent = KOBJ_DEFAULT(ramfs_dirent);

struct ramfs_data {
	struct kobj _header;
	struct hash inodes;
	_Atomic uint64_t next_id;
};

static int _read_page(struct inode *node, int pagenumber, uintptr_t phys)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct frame *frame = hash_lookup(&ri->data, &pagenumber, sizeof(int));
	if(!frame) {
		frame = frame_get_from_address(frame_allocate());
		frame->pagenr = pagenumber;
		hash_insert(&ri->data, &frame->pagenr, sizeof(frame->pagenr), &frame->elem, frame);
	}
	memcpy((void *)(phys + PHYS_MAP_START), (void *)(frame_get_physical(frame) + PHYS_MAP_START), arch_mm_page_size(0));
	mutex_release(&ri->lock);
	return 0;
}

static int _write_page(struct inode *node, int pagenumber, uintptr_t phys)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct frame *frame = hash_lookup(&ri->data, &pagenumber, sizeof(int));
	if(!frame) {
		frame = frame_get_from_address(frame_allocate());
		frame->pagenr = pagenumber;
		hash_insert(&ri->data, &frame->pagenr, sizeof(frame->pagenr), &frame->elem, frame);
	}
	memcpy((void *)(frame_get_physical(frame) + PHYS_MAP_START), (void *)(phys + PHYS_MAP_START), arch_mm_page_size(0));
	mutex_release(&ri->lock);
	return 0;
}

static int _load_inode(struct filesystem *fs, uint64_t ino, struct inode *node)
{
	struct ramfs_data *rfs = fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &ino, sizeof(uint64_t));

	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	node->mode = ri->mode;
	node->atime = ri->atime;
	node->mtime = ri->mtime;
	node->ctime = ri->ctime;
	node->length = ri->length;
	mutex_release(&ri->lock);
	
	return 0;
}

static int _alloc_inode(struct filesystem *fs, uint64_t *id)
{
	struct ramfs_data *rfs = fs->fsdata;
	struct ramfs_inode *ri = kobj_allocate(&kobj_ramfs_inode);
	*id = ri->id = ++rfs->next_id;
	hash_insert(&rfs->inodes, &ri->id, sizeof(ri->id), &ri->elem, ri);
	return 0;
}

static int _lookup(struct inode *node, const char *name, size_t namelen, struct dirent *dir)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct ramfs_dirent *rd = hash_lookup(&ri->dirents, name, namelen);
	if(!rd) {
		mutex_release(&ri->lock);
		return -1;
	}
	strncpy(dir->name, name, namelen);
	dir->namelen = namelen;
	dir->ino.fsid = node->fs->id;
	dir->ino.inoid = rd->ino;
	mutex_release(&ri->lock);
	return 0;
}

static int _link(struct inode *node, const char *name, size_t namelen, struct inode *target)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	struct ramfs_inode *rt = hash_lookup(&rfs->inodes, &target->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);
	assert(rt != NULL);
	assert(node->fs == target->fs);

	mutex_acquire(&ri->lock);
	struct ramfs_dirent *dir = kobj_allocate(&kobj_ramfs_dirent);
	strncpy(dir->name, name, namelen);
	dir->namelen = namelen;
	dir->ino = rt->id;

	if(hash_insert(&ri->dirents, dir->name, dir->namelen, &dir->elem, dir) == -1) {
		mutex_release(&ri->lock);
		kobj_putref(dir);
		return -1;
	}

	mutex_release(&ri->lock);
	return 0;
}

static struct inode_ops ramfs_inode_ops = {
	.read_page = _read_page,
	.write_page = _write_page,
	.sync = NULL,
	.update = NULL,
	.lookup = _lookup,
	.link = _link,
};

static struct fs_ops ramfs_fs_ops = {
	.load_inode = _load_inode,
	.alloc_inode = _alloc_inode,
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
	struct ramfs_inode *root = kobj_allocate(&kobj_ramfs_inode);
	root->id = 0;
	hash_insert(&ramfs_data->inodes, &root->id, sizeof(root->id), &root->elem, root);
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

#include <fs/sys.h>
#include <fs/stat.h>
#include <fs/dirent.h>
void initial_rootfs_init(void)
{
	current_thread->process->root = kobj_allocate(&kobj_filesystem);
	current_thread->process->root->driver = &ramfs;
	current_thread->process->root->fsdata = initial_ramfs;

	struct dirent *dir = kobj_allocate(&kobj_dirent);
	dir->name[0] = '/';
	dir->namelen = 1;
	dir->ino.inoid = 0;
	dir->ino.fsid = current_thread->process->root->id;

	current_thread->process->cwd = dir;
	int i=0;
	struct boot_module *bm;
	while((bm = machine_get_boot_module(i++))) {
		printk(" * Loading %s, %ld KB.\n", bm->name, bm->length / 1024);
		int f = sys_open(bm->name, O_RDWR | O_CREAT, S_IFREG | 0777);
		ssize_t count = sys_pwrite(f, (void *)bm->start, bm->length, 0);
		assert(count == (ssize_t)bm->length);
		sys_close(f);
	}

}

