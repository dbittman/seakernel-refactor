#pragma once
#include <stddef.h>
#include <fs/socket.h>
struct inode;
int fs_link(struct inode *, const char *, size_t, struct inode *);

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

#define F_READ  1
#define F_WRITE 2

#define O_CREAT  0100
#define O_EXCL   0200
#define O_NOCTTY 0400
#define O_TRUNC  01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_CLOEXEC 02000000

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4

struct iovec {
	void *base;
	size_t len;
};

int sys_open(const char *, int, int);
int sys_mknod(const char *path, int mode, dev_t dev);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_pwrite(int fd, void *buf, size_t count, size_t);
ssize_t sys_pread(int fd, void *buf, size_t count, size_t);
ssize_t sys_write(int fd, void *buf, size_t count);
ssize_t sys_readv(int fd, struct iovec *iov, int iovc);
ssize_t sys_writev(int fd, struct iovec *iov, int iovc);
ssize_t sys_preadv(int fd, struct iovec *iov, int iovc, size_t off);
ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, size_t off);
int sys_close(int fd);
int sys_pipe(int *fds);

int sys_socket(int domain, int type, int protocol);
int sys_socketpair(int domain, int type, int protocol, int *sv);
int sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int sys_listen(int sockfd, int backlog);

