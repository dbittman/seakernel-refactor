#pragma once
#include <stddef.h>

struct inode;
int fs_link(struct inode *, const char *, size_t, struct inode *);

#define O_RDONLY 1
#define O_WRONLY 2
#define O_RDWR   3

#define O_CREAT  4

int sys_open(const char *, int, int);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_pwrite(int fd, size_t off, void *buf, size_t count);
ssize_t sys_pread(int fd, size_t off, void *buf, size_t count);
ssize_t sys_write(int fd, void *buf, size_t count);
int sys_close(int fd);
