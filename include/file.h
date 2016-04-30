#pragma once

#include <slab.h>
#include <fs/dirent.h>
#include <blocklist.h>
struct dirent;
struct file_calls;

enum file_device_type {
	FDT_UNKNOWN=0,
	FDT_CHAR,
	FDT_BLOCK,
	FDT_SOCK,
	FDT_REG,
	FDT_FIFO,
};

struct file {
	struct kobj_header _header;
	struct dirent *dirent;
	_Atomic size_t pos;
	_Atomic int flags;
	struct file_calls *ops;
	void *devdata;
	enum file_device_type devtype;
};

extern struct kobj kobj_file;

struct file *process_get_file(int fd);
void process_release_fd(int fd);
int process_allocate_fd(struct file *file, int);
ssize_t file_read(struct file *f, size_t off, size_t len, char *buf);
ssize_t file_write(struct file *f, size_t off, size_t len, const char *buf);
int file_truncate(struct file *f, size_t len);
size_t file_get_len(struct file *f);
void file_close(struct file *file);
struct file *process_exchange_fd(struct file *file, int);
struct file *file_create(struct dirent *dir, enum file_device_type);

struct process;
void process_remove_proc_fd(struct process *proc, int fd);
void process_copy_proc_fd(struct process *from, struct process *to, int fromfd, int tofd);
void process_create_proc_fd(struct process *proc, int fd, const char *path);

static inline struct inode *file_get_inode(struct file *f)
{
	if(f->dirent)
		return dirent_get_inode(f->dirent);
	return NULL;
}

struct mapping;
struct file_calls {
	ssize_t (*read)(struct file *, size_t, size_t, char *);
	ssize_t (*write)(struct file *, size_t, size_t, const char *);

	void (*create)(struct file *);
	void (*destroy)(struct file *);

	int (*select)(struct file *file, int flags, struct blockpoint *bp);
	int (*ioctl)(struct file *file, long cmd, long arg);
	void (*open)(struct file *file);
	void (*close)(struct file *file);

	bool (*map)(struct file *file, struct mapping *map);
	void (*unmap)(struct file *file, struct mapping *map);
};

extern struct file_calls fs_fops;
extern struct file_calls pipe_fops;
extern struct file_calls socket_fops;

#define SEL_READ  0
#define SEL_WRITE 1
#define SEL_ERROR 2

#define FD_SETSIZE 1024

typedef unsigned long fd_mask;

typedef struct
{
	unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

