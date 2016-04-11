#pragma once

#include <slab.h>
#include <fs/dirent.h>
struct dirent;
struct file {
	struct kobj_header _header;
	struct dirent *dirent;
	_Atomic size_t pos;
};

extern struct kobj kobj_file;

struct file *process_get_file(int fd);
void process_release_fd(int fd);
int process_allocate_fd(struct file *file);
ssize_t file_read(struct file *f, size_t off, size_t len, char *buf);
ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf);

static inline struct inode *file_get_inode(struct file *f)
{
	return dirent_get_inode(f->dirent);
}
