#pragma once

#include <slab.h>
#include <fs/dirent.h>
struct dirent;
struct file_calls;
struct file {
	struct kobj_header _header;
	struct dirent *dirent;
	_Atomic size_t pos;
	_Atomic int flags;
	struct file_calls *ops;
	void *devdata;
};

extern struct kobj kobj_file;

struct file *process_get_file(int fd);
void process_release_fd(int fd);
int process_allocate_fd(struct file *file);
ssize_t file_read(struct file *f, size_t off, size_t len, char *buf);
ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf);
int file_truncate(struct file *f, size_t len);
size_t file_get_len(struct file *f);
void file_close(struct file *file);
struct file *file_create(struct dirent *dir, struct file_calls *calls);

static inline struct inode *file_get_inode(struct file *f)
{
	return dirent_get_inode(f->dirent);
}

struct file_calls {
	ssize_t (*read)(struct file *, size_t, size_t, char *);
	ssize_t (*write)(struct file *, size_t, size_t, const char *);

	void (*create)(struct file *);
	void (*destroy)(struct file *);

	int (*select)(struct file *file, int flags, struct blockpoint *bp);
	int (*ioctl)(struct file *file, long cmd, long arg);
	void (*open)(struct file *file);
	void (*close)(struct file *file);
};

struct file_calls *file_get_ops(struct inode *node);
extern struct file_calls fs_fops;
extern struct file_calls pipe_fops;
extern struct file_calls socket_fops;

