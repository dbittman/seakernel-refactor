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
#include <errno.h>

struct ramfs_inode {
	struct kobj_header _header;
	struct hash data;
	struct hash dirents;
	int mode, atime, mtime, ctime;
	int links;
	int uid, gid;
	struct hashelem elem;
	uint64_t id;
	uint32_t major, minor;
	struct mutex lock;
	_Atomic size_t length;
};

struct ramfs_dirent {
	struct kobj_header _header;
	char name[256];
	size_t namelen;
	uint64_t ino;
	struct hashelem elem;
};

static void _ramfs_inode_init(void *obj)
{
	struct ramfs_inode *i = obj;
	i->length = 0;
	i->major = i->minor = 0;
	i->links = 0;
}

static void _ramfs_inode_create(void *obj)
{
	struct ramfs_inode *i = obj;
	hash_create(&i->data, 0, 256);
	hash_create(&i->dirents, 0, 256);
	mutex_create(&i->lock);
	_ramfs_inode_init(obj);
}

struct ramfs_data_block {
	struct kobj_header _header;
	struct hashelem elem;
	uintptr_t phys;
	int pagenum;
};

static void _ramfs_inode_destroy(void *obj)
{
	struct ramfs_inode *i = obj;
	hash_destroy(&i->dirents);
	hash_destroy(&i->data);
}

static void _ramfs_inode_put(void *obj)
{
	struct ramfs_inode *i = obj;
	struct hashiter iter;
	for(hash_iter_init(&iter, &i->data); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct ramfs_data_block *block = hash_iter_get(&iter);
		hash_delete(&i->data, &block->pagenum, sizeof(int));
		mm_physical_deallocate(block->phys);
		kobj_putref(block);
	}
	for(hash_iter_init(&iter, &i->dirents); !hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct ramfs_dirent *d = hash_iter_get(&iter);
		hash_delete(&i->dirents, d->name, d->namelen);
		kobj_putref(d);
	}
}

struct kobj kobj_ramfs_inode = {
	KOBJ_DEFAULT_ELEM(ramfs_inode),
	.put = _ramfs_inode_put, .destroy = _ramfs_inode_destroy,
	.create = _ramfs_inode_create, .init = _ramfs_inode_init,
};

struct kobj kobj_ramfs_dirent = KOBJ_DEFAULT(ramfs_dirent);

struct ramfs_data {
	struct kobj_header _header;
	struct hash inodes;
	_Atomic uint64_t next_id;
};
struct kobj kobj_ramfs_data_block = KOBJ_DEFAULT(ramfs_data_block);

static int _read_page(struct inode *node, int pagenumber, uintptr_t phys)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct ramfs_data_block *block = hash_lookup(&ri->data, &pagenumber, sizeof(int));
	if(!block) {
		block = kobj_allocate(&kobj_ramfs_data_block);
		block->phys = mm_physical_allocate(arch_mm_page_size(0), true);
		block->pagenum = pagenumber;
		hash_insert(&ri->data, &block->pagenum, sizeof(int), &block->elem, block);
	}
	memcpy((void *)(phys + PHYS_MAP_START), (void *)(block->phys + PHYS_MAP_START), arch_mm_page_size(0));
	mutex_release(&ri->lock);
	return 0;
}

static int _write_page(struct inode *node, int pagenumber, uintptr_t phys)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct ramfs_data_block *block = hash_lookup(&ri->data, &pagenumber, sizeof(int));
	if(!block) {
		block = kobj_allocate(&kobj_ramfs_data_block);
		block->phys = mm_physical_allocate(arch_mm_page_size(0), true);
		block->pagenum = pagenumber;
		hash_insert(&ri->data, &block->pagenum, sizeof(int), &block->elem, block);
	}
	memcpy((void *)(block->phys + PHYS_MAP_START), (void *)(phys + PHYS_MAP_START), arch_mm_page_size(0));
	mutex_release(&ri->lock);
	return 0;
}

static int _load_inode(struct filesystem *fs, uint64_t ino, struct inode *node)
{
	struct ramfs_data *rfs = fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &ino, sizeof(uint64_t));

	if(ri == NULL)
		return -ENOENT;

	mutex_acquire(&ri->lock);
	node->mode = ri->mode;
	node->atime = ri->atime;
	node->mtime = ri->mtime;
	node->ctime = ri->ctime;
	node->links = ri->links;
	node->length = ri->length;
	node->uid = ri->uid;
	node->gid = ri->gid;
	node->id.inoid = ino;
	node->major = ri->major;
	node->minor = ri->minor;
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
	assert(rfs != NULL);
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct ramfs_dirent *rd = hash_lookup(&ri->dirents, name, namelen);
	if(!rd) {
		mutex_release(&ri->lock);
		return -ENOENT;
	}
	if(dir) {
		strncpy(dir->name, name, namelen);
		dir->namelen = namelen;
		dir->ino.fsid = node->fs->id;
		dir->ino.inoid = rd->ino;
	}
	mutex_release(&ri->lock);
	return 0;
}

static int _unlink(struct inode *node, const char *name, size_t namelen)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	assert(rfs != NULL);
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);

	mutex_acquire(&ri->lock);
	struct ramfs_dirent *rd = hash_lookup(&ri->dirents, name, namelen);
	if(rd) {
		hash_delete(&ri->dirents, name, namelen);
		kobj_putref(rd);
	}
	mutex_release(&ri->lock);
	return 0;
}

static size_t _getdents(struct inode *node, _Atomic size_t *start, struct gd_dirent *gd, size_t count)
{
	struct ramfs_data *rfs = node->fs->fsdata;
	assert(rfs != NULL);
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);

	size_t read = 0;
	struct hashiter iter;
	char *rec = (char *)gd;
	__hash_lock(&ri->dirents);
	for(hash_iter_init(&iter, &ri->dirents);
			!hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct ramfs_dirent *rd = hash_iter_get(&iter);

		int reclen = rd->namelen + sizeof(struct gd_dirent) + 1;
		reclen = (reclen & ~15) + 16;

		if(read >= *start) {
			if(reclen + (read - *start) > count)
				break;
			struct gd_dirent *dp = (void *)rec;
			dp->d_reclen = reclen;
			memcpy(dp->d_name, rd->name, rd->namelen);
			dp->d_name[rd->namelen] = 0;
			dp->d_off = read + reclen + *start;
			dp->d_type = 0;
			dp->d_ino = rd->ino;

			rec += reclen;
		}
		read += reclen;
	}

	__hash_unlock(&ri->dirents);
	*start += (uintptr_t)rec - (uintptr_t)gd;
	return (uintptr_t)rec - (uintptr_t)gd;
}

static int __ramfs_do_link(struct ramfs_inode *ri, const char *name, size_t namelen, struct ramfs_inode *rt)
{
	mutex_acquire(&ri->lock);
	struct ramfs_dirent *dir = kobj_allocate(&kobj_ramfs_dirent);
	strncpy(dir->name, name, namelen);
	dir->namelen = namelen;
	dir->ino = rt->id;

	if(hash_insert(&ri->dirents, dir->name, dir->namelen, &dir->elem, dir) == -1) {
		mutex_release(&ri->lock);
		kobj_putref(dir);
		return -EEXIST;
	}

	rt->links++;

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

	return __ramfs_do_link(ri, name, namelen, rt);
}

#include <fs/proc.h>
static int _readlink(struct inode *node, char *path, size_t len)
{
	if(node->major) {
		ssize_t ret = proc_read_data(node->minor, 0, len, path);
		if(ret >= 0) {
			path[ret] = 0;
			return 0;
		}
	}
	int r = inode_do_read_data(node, 0, len, path);
	path[r] = 0;
	return 0;
}

static int _writelink(struct inode *node, const char *path)
{
	inode_do_write_data(node, 0, strlen(path), path);
	return 0;
}

static struct inode_ops ramfs_inode_ops = {
	.read_page = _read_page,
	.write_page = _write_page,
	.sync = NULL,
	.update = NULL,
	.lookup = _lookup,
	.link = _link,
	.getdents = _getdents,
	.unlink = _unlink,
	.readlink = _readlink,
	.writelink = _writelink,
};

static void _ramfs_create(void *obj)
{
	struct ramfs_data *ramfs_data = obj;
	ramfs_data->next_id = 1;
	hash_create(&ramfs_data->inodes, 0, 256);
	struct ramfs_inode *root = kobj_allocate(&kobj_ramfs_inode);
	root->id = 1;
	root->mode = S_IFDIR | 0755;
	root->uid = root->gid = 0;
	root->atime = root->mtime = root->ctime = arch_time_getepoch();
	hash_insert(&ramfs_data->inodes, &root->id, sizeof(root->id), &root->elem, root);

	__ramfs_do_link(root, ".", 1, root);
	__ramfs_do_link(root, "..", 2, root);
}

static void _ramfs_destroy(void *obj)
{
	struct ramfs_data *ramfs_data = obj;
	struct hashiter iter;
	for(hash_iter_init(&iter, &ramfs_data->inodes);
			!hash_iter_done(&iter); hash_iter_next(&iter)) {
		struct ramfs_inode *node = hash_iter_get(&iter);
		hash_delete(&ramfs_data->inodes, &node->id, sizeof(node->id));
		kobj_putref(node);
	}
	hash_destroy(&ramfs_data->inodes);
}

static int _update_inode(struct filesystem *fs, struct inode *node)
{
	struct ramfs_data *rfs = fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));

	if(ri == NULL)
		return -ENOENT;

	mutex_acquire(&ri->lock);
	ri->mode = node->mode;
	ri->atime = node->atime;
	ri->mtime = node->mtime;
	ri->ctime = node->ctime;
	ri->links = node->links;
	ri->length = node->length;
	ri->uid = node->uid;
	ri->gid = node->gid;
	ri->major = node->major;
	ri->minor = node->minor;
	mutex_release(&ri->lock);
	
	return 0;
}

static void _release_inode(struct filesystem *fs, struct inode *node)
{
	struct ramfs_data *rfs = fs->fsdata;
	struct ramfs_inode *ri = hash_lookup(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(ri != NULL);
	int r = hash_delete(&rfs->inodes, &node->id.inoid, sizeof(uint64_t));
	assert(r == 0);
	kobj_putref(ri);
}

static struct kobj kobj_ramfs = {
	KOBJ_DEFAULT_ELEM(ramfs_data),
	.create = _ramfs_create,
	.destroy = _ramfs_destroy,
	.init = NULL,
	.put = NULL,
};
static int _mount(struct filesystem *fs, struct blockdev *bd, unsigned long flags)
{
	(void)flags;
	if(bd)
		return -EINVAL;
	fs->fsdata = kobj_allocate(&kobj_ramfs);
	return 0;
}

static struct ramfs *initial_ramfs;

__orderedinitializer(__orderedafter(FILESYSTEM_INIT_ORDER))
static void _init_ramfs(void)
{
	initial_ramfs = kobj_allocate(&kobj_ramfs);
}
static struct fs_ops ramfs_fs_ops = {
	.load_inode = _load_inode,
	.alloc_inode = _alloc_inode,
	.release_inode = _release_inode,
	.update_inode = _update_inode,
	.mount = _mount,
};

struct fsdriver ramfs = {
	.inode_ops = &ramfs_inode_ops,
	.fs_ops = &ramfs_fs_ops,
	.name = "ramfs",
	.rootid = 1,
};


#include <fs/sys.h>
#include <fs/stat.h>
#include <fs/dirent.h>
#include <device.h>

struct ustar_header {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char checksum[8];
        char typeflag[1];
        char linkname[100];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        char prefix[155];
        char pad[12];
};

static void parse_tar_file(char *start, size_t tarlen)
{
	struct ustar_header *uh = (struct ustar_header *)start;
	while((char *)uh < start + tarlen) {
		int err;
		char *name = uh->name;
		if(!*name)
			break;
		/* convert from ascii octal (wtf) to FUCKING NUMBERS */
		size_t len = strtol(uh->size, NULL, 8);
		size_t reclen = (len + 511) & ~511;
		char *data_start = (char *)uh + 512;
		if(strncmp(uh->magic, "ustar", 5))
			break;

		printk("   - loading '%s': %ld bytes...\n", uh->name, len);
		switch(uh->typeflag[0]) {
			int fd;
			case '2':
				printk("SYMLINK\n");
				break;
			case '5':
				uh->name[strlen(uh->name) - 1] = 0;
				err = sys_mkdir(uh->name, 0777);
				if(err && err != -EEXIST)
					printk("     failed: %d\n", err);
				break;
			case '0': case '7':
				fd = sys_open(uh->name, O_CREAT | O_EXCL | O_RDWR, S_IFREG | 0777);
				if(fd >= 0) {
					sys_pwrite(fd, data_start, len, 0);
					sys_close(fd);
				} else {
					printk("     failed: %d\n", fd);
				}
				break;
			default:
				printk("initrd: unknown file type %c\n", uh->typeflag[0]);

		}

		uh = (struct ustar_header *)((char *)uh + 512 + reclen);
	}
}

void initial_rootfs_init(void)
{
	filesystem_register(&ramfs);
	struct filesystem *fs = kobj_allocate(&kobj_filesystem);
	fs->driver = &ramfs;
	fs->fsdata = initial_ramfs;

	struct inode_id id = { .fsid = fs->id, .inoid = 1 };

	struct inode *node = inode_lookup(&id);
	assert(node != NULL);


	current_thread->process->cwd = kobj_getref(node);
	current_thread->process->root = node;

	int i=0;
	struct boot_module *bm;
	while((bm = machine_get_boot_module(i++))) {
		/* we assume all boot modules are tar files */
		printk(" * Loading %s, %ld KB.\n", bm->name, bm->length / 1024);
		parse_tar_file((void *)bm->start, bm->length);
	}

	sys_mkdir("/mnt", 0777);
	sys_mkdir("/dev", 0777);
	sys_mkdir("/proc", 0777);

	sys_mount(NULL, "/dev", "ramfs", 0, NULL);
	sys_mount(NULL, "/proc", "ramfs", 0, NULL);
}

