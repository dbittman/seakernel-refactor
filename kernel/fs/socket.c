#include <device.h>
#include <file.h>
#include <fs/socket.h>
#include <errno.h>
#include <fs/sys.h>

extern struct sock_calls af_unix_calls;

static void _socket_init(void *obj)
{
	struct socket *sock = obj;
	sock->flags = 0;
	sock->ops = NULL;
}

static void _socket_create(void *obj)
{
	struct socket *s = obj;
	linkedlist_create(&s->pend_con, 0);
	blocklist_create(&s->pend_con_wait);
	_socket_init(obj);
}

static struct kobj kobj_socket = {
	KOBJ_DEFAULT_ELEM(socket),
	.create = _socket_create, .destroy = NULL,
	.init = _socket_init, .put = NULL,
};

static struct sock_calls *domains[MAX_AF] = {
	[0] = NULL,
	[AF_UNIX] = &af_unix_calls,
};

struct socket *socket_get_from_fd(int fd, int *err)
{
	struct file *file = process_get_file(fd);
	if(!file) {
		*err = -EBADF;
		return NULL;
	}
	if(file->devtype != FDT_SOCK) {
		*err = -ENOTSOCK;
		kobj_putref(file);
		return NULL;
	}
	struct socket *sock = kobj_getref(file->devdata);
	kobj_putref(file);
	return sock;
}

sysret_t sys_socket(int domain, int type, int protocol)
{
	if(domain >= MAX_AF)
		return -EINVAL;
	struct file *file = file_create(NULL, FDT_SOCK);
	file->flags |= F_READ | F_WRITE;
	int fd = process_allocate_fd(file);
	if(fd < 0) {
		kobj_putref(file);
		return -EMFILE;
	}
	struct socket *sock = file->devdata;
	sock->domain = domain;
	sock->type = type;
	sock->protocol = protocol;
	sock->ops = domains[domain];
	if(sock->ops->init) sock->ops->init(sock);
	kobj_putref(file);
	return fd;
}

sysret_t sys_socketpair(int domain, int type, int protocol, int *sv)
{
	struct file *f1 = file_create(NULL, FDT_SOCK);
	struct file *f2 = file_create(NULL, FDT_SOCK);

	f1->flags |= F_READ | F_WRITE;
	f2->flags |= F_READ | F_WRITE;
	int fd1 = process_allocate_fd(f1);
	if(fd1 < 0) {
		kobj_putref(f1);
		kobj_putref(f2);
		return -EMFILE;
	}
	int fd2 = process_allocate_fd(f2);
	if(fd2 < 0) {
		kobj_putref(f1);
		kobj_putref(f2);
		process_release_fd(fd1);
		return -EMFILE;
	}

	struct socket *s1 = f1->devdata;
	struct socket *s2 = f2->devdata;
	s1->domain = s2->domain = domain;
	s1->type = s2->type = type;
	s2->protocol = s1->protocol = protocol;

	s1->ops = s2->ops = domains[domain];
	if(s1->ops->init) s1->ops->init(s1);
	if(s2->ops->init) s2->ops->init(s2);
	s1->ops->sockpair(s1, s2);
	kobj_putref(f1);
	kobj_putref(f2);

	sv[0] = fd1;
	sv[1] = fd2;

	return 0;
}

sysret_t sys_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	if(socket->ops->accept)
		err = socket->ops->accept(socket, addr, addrlen);

	kobj_putref(socket);
	return err;
}

sysret_t sys_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	if(socket->ops->connect)
		err = socket->ops->connect(socket, addr, addrlen);

	kobj_putref(socket);
	return err;
}

sysret_t sys_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	if(socket->ops->bind)
		err = socket->ops->bind(socket, addr, addrlen);
	if(!err)
		socket->flags |= SF_BOUND;
	kobj_putref(socket);
	return err;
}

sysret_t sys_listen(int sockfd, int backlog)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	if(socket->ops->listen)
		err = socket->ops->listen(socket, backlog);
	if(!err) {
		socket->flags |= SF_LISTEN;
		socket->backlog = backlog;
	}
	kobj_putref(socket);
	return err;
}

ssize_t _do_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	if(sock->flags & SF_SHUTDOWN)
		return 0;
	if(!(sock->flags & SF_CONNEC)) //TODO: or connectionless
		return -ENOTCONN;
	if(sock->ops->recv)
		return sock->ops->recv(sock, buf, len, flags);
	return -ENOTSUP;
}

ssize_t _do_send(struct socket *sock, const char *buf, size_t len, int flags)
{
	if(sock->flags & SF_SHUTDOWN)
		return -EPIPE; //TODO: sigpipe
	if(!(sock->flags & SF_CONNEC)) //TODO: or connectionless
		return -ENOTCONN;
	if(sock->ops->send)
		return sock->ops->send(sock, buf, len, flags);
	return -ENOTSUP;
}

sysret_t sys_recv(int sockfd, char *buf, size_t len, int flags)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	ssize_t ret = _do_recv(socket, buf, len, flags);
	kobj_putref(socket);
	return ret;
}

sysret_t sys_send(int sockfd, const char *buf, size_t len, int flags)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;
	
	size_t ret = _do_send(socket, buf, len, flags);
	kobj_putref(socket);
	return ret;
}

sysret_t sys_recvfrom(int sockfd, char *buf, size_t len, int flags, struct sockaddr *src, socklen_t *srclen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	ssize_t ret = -ENOTSUP;
	if(socket->flags & SF_CONNEC)
		ret = _do_recv(socket, buf, len, flags);
	else if(socket->ops->recvfrom)
		ret = socket->ops->recvfrom(socket, buf, len, flags, src, srclen);
	kobj_putref(socket);
	return ret;
}

sysret_t sys_sendto(int sockfd, const char *buf, size_t len, int flags, const struct sockaddr *dest, socklen_t addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;
	
	ssize_t ret = -ENOTSUP;
	if(socket->flags & SF_CONNEC)
		ret = _do_send(socket, buf, len, flags);
	else if(socket->ops->sendto)
		ret = socket->ops->sendto(socket, buf, len, flags, dest, addrlen);
	kobj_putref(socket);
	return ret;
}

static void _socket_fops_create(struct file *file)
{
	file->devdata = kobj_allocate(&kobj_socket);
}

static void _socket_fops_destroy(struct file *file)
{
	if(file->devtype == FDT_SOCK) {
		struct socket *sock = file->devdata;
		if(sock->ops->shutdown)
			sock->ops->shutdown(sock);
		kobj_putref(file->devdata);
	}
}

static ssize_t _socket_read(struct file *file, size_t off, size_t len, char *buf)
{
	(void)off;
	assert(file->devtype == FDT_SOCK);
	struct socket *sock = kobj_getref(file->devdata);
	size_t ret = _do_recv(sock, buf, len, 0);
	kobj_putref(sock);
	return ret;
}

static ssize_t _socket_write(struct file *file, size_t off, size_t len, const char *buf)
{
	(void)off;
	assert(file->devtype == FDT_SOCK);
	struct socket *sock = kobj_getref(file->devdata);
	ssize_t ret = _do_send(sock, buf, len, 0);
	kobj_putref(sock);
	return ret;
}

static int _socket_select(struct file *file, int flags, struct blockpoint *bp)
{
	struct socket *sock = kobj_getref(file->devdata);

	int ret = 1;
	if(sock->ops->select)
		ret = sock->ops->select(sock, flags, bp);

	kobj_putref(sock);
	return ret;
}

struct file_calls socket_fops = {
	.write = _socket_write,
	.read = _socket_read,
	.create = _socket_fops_create,
	.destroy = _socket_fops_destroy,
	.ioctl = 0, .select = _socket_select, .open = 0, .close = 0,
};

