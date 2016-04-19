#include <fs/path.h>
#include <fs/dirent.h>
#include <thread.h>
#include <process.h>

sysret_t sys_getcwd(char *buf, size_t size)
{
	*buf = '/';
	*(buf+1) = 0;
	(void)size;
	return 0;
	/*
	struct dirent *dir = kobj_getref(current_thread->process->cwd);
	struct inode *node = dirent_get_inode(dir);

	if(dir->namelen+1 >= size) {
		kobj_putref(dir);
		inode_put(node);
		return -ERANGE;
	}

	memcpy(buf, dir->name, dir->namelen);
	*(buf + dir->namelen) = '/';
	int pos = dir->namelen + 1;
	kobj_putref(dir);

	while(1) {
		struct inode *next_inode;
		struct dirent *next_dirent;
		int err = fs_path_resolve("..", node, 0, 0, &next_dirent, &next_node);
		inode_put(node);
		if(err) {
			return err;
		}
		
		memcpy(buf+pos, next_dirent->name, next_dirent->namelen);
		*(buf+pos+next_dirent->namelen) = '/';

		node = next_inode;
	}
	*/
}

