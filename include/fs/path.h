#pragma once

#define PATH_CREATE 1
#define PATH_NOFOLLOW 2

#define PATH_DID_CREATE 1

struct dirent;
struct inode;
int fs_path_resolve(const char *path, struct inode *_start, int flags, int mode, struct dirent **dir_out, struct inode **ino_out);
int fs_unlink(struct inode *node, const char *name, size_t namelen);
int fs_link(struct inode *node, const char *name, size_t namelen, struct inode *target);
int fs_rmdir(struct inode *node, const char *name, size_t namelen);
