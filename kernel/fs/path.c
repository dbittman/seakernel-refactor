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
#include <errno.h>
TRACE_DEFINE(path_trace, "path");

struct dirent *follow_symlink(struct inode *node, int *err)
{
	char path[256];
	struct dirent *dir = NULL;
	if(S_ISLNK(node->mode)) {
		memset(path, 0, 256);
		inode_do_read_data(node, 0, 255, path);
		/* TODO: follow by default? */
		*err = fs_path_resolve(path, node, PATH_SYMLINK, 0, &dir, NULL);
		if(*err < 0)
			return NULL;
	}
	return dir;
}

static struct dirent *inode_lookup_dirent(struct inode *node, const char *name, size_t namelen, int *err)
{
	TRACE(&path_trace, "lookup dirent: %ld, %s %d", node->id.inoid, name, namelen);
	struct dirent *dir = kobj_allocate(&kobj_dirent);
	mutex_acquire(&node->lock);
	if((*err = node->fs->driver->inode_ops->lookup(node, name, namelen, dir)) == 0) {
		mutex_release(&node->lock);
		strncpy(dir->name, name, namelen);
		dir->namelen = namelen;
		return dir;
	}
	mutex_release(&node->lock);
	kobj_putref(dir);
	return NULL;
}

struct dirent *__create_last(struct inode *node, const char *name, size_t namelen, int mode, int *err)
{
	// test node is dir
	TRACE(&path_trace, "creating new entry %s %d\n", name, namelen);
	uint64_t inoid;
	if((mode & S_IFMT) == 0)
		mode |= S_IFREG;
	if(!inode_check_perm(node, PERM_WRITE)) {
		*err = -EACCES;
		return NULL;
	}
	mutex_acquire(&node->fs->lock);
	if((*err = node->fs->driver->fs_ops->alloc_inode(node->fs, &inoid)) < 0) {
		mutex_release(&node->fs->lock);
		return NULL;
	}
	mutex_release(&node->fs->lock);
	
	struct inode *target = fs_inode_lookup(node->fs, inoid);
	target->mode = mode;
	target->uid = current_thread->process->euid;
	target->gid = current_thread->process->egid;
	inode_mark_dirty(target);

	if((*err = fs_link(node, name, namelen, target)) < 0) {
		inode_put(target);
		return NULL;
	}

	if(S_ISDIR(mode)) {
		fs_link(target, ".", 1, target);
		fs_link(target, "..", 2, node);
	}
	
	struct dirent *dir = inode_lookup_dirent(node, name, namelen, err);
	inode_put(target);
	return dir;
}

int fs_path_resolve(const char *path, struct inode *_start, int flags, int mode, struct dirent **dir_out, struct inode **ino_out)
{
	(void)flags;
	TRACE(&path_trace, "resolve path %s", path);
	struct dirent *dir = NULL;
	struct inode *start = NULL;
	if(_start) {
		start = kobj_getref(_start);
	} else {
		dir = kobj_getref(current_thread->process->cwd);
		start = dirent_get_inode(dir);
	}
	if(*path == '/') {
		path++;
		inode_put(start);
		if(!(start = fs_inode_lookup(current_thread->process->root, current_thread->process->root->driver->rootid))) {
			return -ENOENT;
		}
	}

	assert(start != NULL);

	const char *sep;
	const char *name = path;
	struct inode *node = start;
	int returnval = 0;
	do { 
		if(!(sep = strchrc(name, '/'))) {
			sep = path + strlen(path);
		}
		TRACE(&path_trace, "next segment: %s %d %c\n", name, sep - name, *sep == 0 ? ' ' : '/');
		if(sep != name) {
			int err;
			if(dir)
				kobj_putref(dir);

			if(!inode_check_perm(node, PERM_READ)) {
				inode_put(node);
				return -EACCES;
			}

			dir = inode_lookup_dirent(node, name, sep - name, &err);
			TRACE(&path_trace, "lookup returned %p", dir);
			if(!dir) {
				if(!*sep && (flags & PATH_CREATE)) {
					dir = __create_last(node, name, sep - name, mode, &err);
					if(dir != 0)
						returnval |= PATH_DID_CREATE;
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
	else if(dir)
		kobj_putref(dir);

	if(ino_out)
		*ino_out = node;
	else
		inode_put(node);

	return returnval;
}

#include <system.h>
__initializer static void _fs_path_trace(void)
{
	//trace_enable(&path_trace);
}

