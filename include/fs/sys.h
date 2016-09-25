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
#define O_NOFOLLOW   0400000
#define O_CLOEXEC   02000000
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_SETOWN 8
#define F_GETOWN 9

#define F_UNLCK 2
struct iovec {
	void *base;
	size_t len;
};

struct flock
{
	short l_type;
	short l_whence;
	ssize_t l_start;
	ssize_t l_len;
	int l_pid;
};

sysret_t sys_open(const char *, int, int);
sysret_t sys_mknod(const char *path, int mode, dev_t dev);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_pwrite(int fd, void *buf, size_t count, size_t);
ssize_t sys_pread(int fd, void *buf, size_t count, size_t);
ssize_t sys_write(int fd, void *buf, size_t count);
ssize_t sys_readv(int fd, struct iovec *iov, int iovc);
ssize_t sys_writev(int fd, struct iovec *iov, int iovc);
ssize_t sys_preadv(int fd, struct iovec *iov, int iovc, size_t off);
ssize_t sys_pwritev(int fd, struct iovec *iov, int iovc, size_t off);
sysret_t sys_close(int fd);
sysret_t sys_pipe(int *fds);
sysret_t sys_dup2(int old, int new);
sysret_t sys_dup(int old);
sysret_t sys_getcwd(char *buf, size_t size);
sysret_t sys_mkdir(const char *path, int mode);
sysret_t sys_access(const char *path, int mode);
sysret_t sys_lseek(int fd, ssize_t off, int whence);
sysret_t sys_lstat(const char *path, struct stat *buf);
sysret_t sys_chdir(const char *path);
sysret_t sys_fchdir(int fd);
sysret_t sys_fadvise(int fd, ssize_t offset, size_t len, int advice);
ssize_t sys_readlink(const char *, char *, size_t);
#define MS_BIND 4096
sysret_t sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, const void *data);
sysret_t sys_chroot(const char *path);
sysret_t sys_link(const char *, const char *);
sysret_t sys_unlink(const char *_path);

struct stat;
sysret_t sys_stat(const char *path, struct stat *buf);
sysret_t sys_fstat(int fd, struct stat *buf);

sysret_t sys_socket(int domain, int type, int protocol);
sysret_t sys_socketpair(int domain, int type, int protocol, int *sv);
sysret_t sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
sysret_t sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
sysret_t sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
sysret_t sys_listen(int sockfd, int backlog);
sysret_t sys_recvfrom(int sockfd, char *buf, size_t len, int flags, struct sockaddr *src, socklen_t *srclen);
sysret_t sys_sendto(int sockfd, const char *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen);
sysret_t sys_setsockopt(int sockfd, int level, int option, const void *value, socklen_t optlen);
sysret_t sys_getsockopt(int sockfd, int level, int option, void * restrict value, socklen_t * restrict optlen);
sysret_t sys_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
sysret_t sys_getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen);

struct gd_dirent;
sysret_t sys_getdents(int fd, struct gd_dirent *dp, int count);
sysret_t sys_rmdir(const char *path);
sysret_t sys_symlink(const char *target, const char *linkpath);
sysret_t sys_openat(int dirfd, const char *path, int flags, int mode);
sysret_t sys_fchmod(int fd, int mode);
sysret_t sys_fchown(int fd, int owner, int group);
sysret_t sys_fstatat(int dirfd, const char *path, struct stat *buf, int flags);
sysret_t sys_linkat(int olddirfd, const char *targ, int newdirfd, const char *_path);
sysret_t sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
sysret_t sys_rename(const char *oldpath, const char *newpath);
sysret_t sys_faccessat(int fd, const char *path, int mode);
sysret_t sys_umask(int mask);
sysret_t sys_chmod(const char *path, int mode);
sysret_t sys_fchmodat(int dirfd, const char *path, int mode);



#define AT_FDCWD (-100)
#include <thread.h>
#include <process.h>
#include <file.h>
#define AT_GAS_FAILED (void *)(-1)
static inline struct inode *__get_at_start(int fd)
{
	if(fd == AT_FDCWD)
		return kobj_getref(current_thread->process->cwd);
	struct file *file = process_get_file(fd);
	if(!file)
		return NULL;
	struct inode *node = file_get_inode(file);
	kobj_putref(file);
	return node == NULL ? AT_GAS_FAILED : node;
}

#define AT_REMOVEDIR 0x200
sysret_t sys_unlinkat(int dirfd, const char *_path, int flags);
sysret_t sys_lchown(const char *path, int uid, int gid);
sysret_t sys_fchown(int fd, int uid, int gid);
sysret_t sys_chown(const char *path, int uid, int gid);
sysret_t sys_fchownat(int dirfd, const char *path, int uid, int gid, int flags);


struct pollfd {
	int   fd;         /* file descriptor */
	short events;     /* requested events */
	short revents;    /* returned events */
};

sysret_t sys_ppoll(struct pollfd *fds, int nfds, const struct timespec *tmo_p, const sigset_t *sigmask);
sysret_t sys_poll(struct pollfd *fds, int nfds, int timeout);

