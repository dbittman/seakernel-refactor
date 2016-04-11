#pragma once

#define PATH_CREATE 1

int fs_path_resolve(const char *path, struct inode *_start, int flags, int mode, struct dirent **dir_out, struct inode **ino_out);
