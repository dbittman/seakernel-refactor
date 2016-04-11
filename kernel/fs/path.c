#include <fs/dirent.h>
#include <fs/inode.h>
#include <fs/filesystem.h>
#include <string.h>
#include <thread.h>
#include <process.h>
#include <fs/sys.h>
#include <fs/path.h>
#include <trace.h>
#include <printk.h>
TRACE_DEFINE(path_trace, "path");

static struct dirent *inode_lookup_dirent(struct inode *node, const char *name, size_t namelen, int *err)
{
	TRACE(&path_trace, "lookup dirent: %ld, %s %d", node->id.inoid, name, namelen);
	struct dirent *dir = kobj_allocate(&kobj_dirent);
	if((*err = node->fs->driver->inode_ops->lookup(node, name, namelen, dir)) == 0) {
		strncpy(dir->name, name, namelen);
		dir->namelen = namelen;
		return dir;
	}
	kobj_putref(dir);
	return NULL;
}

struct dirent *__create_last(struct inode *node, const char *name, size_t namelen, int mode, int *err)
{
	// test node is dir
	TRACE(&path_trace, "creating new entry %s %d\n", name, namelen);
	uint64_t inoid;
	if((*err = node->fs->driver->fs_ops->alloc_inode(node->fs, &inoid)) < 0) {
		return NULL;
	}
	
	struct inode *target = fs_inode_lookup(node->fs, inoid);
	target->mode = mode;
	inode_mark_dirty(target);

	if((*err = fs_link(node, name, namelen, target)) < 0)
		return NULL;
	
	return inode_lookup_dirent(node, name, namelen, err);
}

int fs_path_resolve(const char *path, struct inode *_start, int flags, int mode, struct dirent **dir_out, struct inode **ino_out)
{
	(void)flags;
	TRACE(&path_trace, "resolve path %s", path);
	struct inode *start = _start ? kobj_getref(_start) : dirent_get_inode(current_thread->process->cwd);
	if(*path == '/') {
		path++;
		kobj_putref(start);
		if(!(start = fs_inode_lookup(current_thread->process->root, current_thread->process->root->driver->rootid))) {
			return -1;
		}
	}

	assert(start != NULL);

	const char *sep;
	const char *name = path;
	struct dirent *dir = NULL;
	struct inode *node = start;
	do { 
		if(!(sep = strchrc(path, '/'))) {
			sep = path + strlen(path);
		}
		TRACE(&path_trace, "next segment: %s %d %c\n", name, sep - name, *sep == 0 ? ' ' : '/');
		if(sep != name) {
			int err;
			dir = inode_lookup_dirent(node, name, sep - name, &err);
			TRACE(&path_trace, "lookup returned %p", dir);
			if(!dir) {
				if(!*sep && (flags & PATH_CREATE)) {
					dir = __create_last(node, name, sep - name, mode, &err);
				}
				if(!dir) {
					inode_put(node);
					return err;
				}
			}
			struct inode *next = dirent_get_inode(dir);
			inode_put(node);
			node = next;
		}

		name = sep + 1;
	} while(*sep != 0);

	if(dir_out)
		*dir_out = dir;
	else
		kobj_putref(dir);

	if(ino_out)
		*ino_out = node;
	else
		inode_put(node);

	return 0;
}

#include <system.h>
__initializer static void _fs_path_trace(void)
{
	trace_enable(&path_trace);
}

