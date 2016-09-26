#include <device.h>
#include <file.h>
#include <fs/socket.h>
#include <errno.h>
#include <fs/sys.h>
#include <thread.h>
#include <printk.h>
#include <net/nic.h>
extern struct sock_calls af_unix_calls;
extern struct sock_calls af_udp_calls;
extern struct sock_calls af_ipv6_calls;
extern struct sock_calls af_tcp_calls;

extern const struct sockaddr_in6 ipv6_any_address;
struct sockaddrinfo sockaddrinfo[MAX_AF + 1] = {
	[AF_INET6] = {.length = sizeof(struct sockaddr_in6), .any_address = (const struct sockaddr *)&ipv6_any_address},
};

static void _socket_init(void *obj)
{
	struct socket *sock = obj;
	sock->flags = 0;
	sock->ops = NULL;
	arena_create(&sock->optarena);
	hash_create(&sock->options, HASH_LOCKLESS, 64);
}

static void _socket_create(void *obj)
{
	struct socket *s = obj;
	linkedlist_create(&s->pend_con, 0);
	blocklist_create(&s->pend_con_wait);
	spinlock_create(&s->optlock);
	_socket_init(obj);
}

static void _socket_put(void *obj)
{
	struct socket *sock = obj;
	hash_destroy(&sock->options);
	arena_destroy(&sock->optarena);
	if(sock->nic)
		kobj_putref(sock->nic);
	sock->nic = NULL;
}

static struct kobj kobj_socket = {
	KOBJ_DEFAULT_ELEM(socket),
	.create = _socket_create, .destroy = NULL,
	.init = _socket_init, .put = _socket_put,
};

static struct sock_calls *domains[MAX_AF + 1][MAX_PROT + 1] = {
	[0] = {NULL, NULL, NULL},
	[AF_UNIX]  = {NULL, &af_unix_calls},
	[AF_INET6] = {[0] = &af_ipv6_calls, [PROT_TCP] = &af_tcp_calls, [PROT_UDP] = &af_udp_calls, [PROT_ICMPV6] = &af_ipv6_calls },
};

static int default_protocol[MAX_AF + 1][MAX_TYPE + 1] = {
	[AF_UNIX]  = {0, 1, 1, 0},
	[AF_INET6] = {0, [SOCK_STREAM] = PROT_TCP, [SOCK_DGRAM] = PROT_UDP, [SOCK_RAW] = 0},
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
	*err = file->flags;
	kobj_putref(file);
	return sock;
}

sysret_t sys_socket(int domain, int type, int protocol)
{
	/* TODO: type can take flags... deal with them. */
	if(domain > MAX_AF || domain < 0)
		return -EINVAL;
	if(protocol > MAX_PROT || protocol < 0)
		return -EINVAL;
	if(type <= 0 || type > MAX_TYPE)
		return -EINVAL;
	protocol = protocol == 0 ? default_protocol[domain][type] : protocol;
	struct sock_calls *ops = domains[domain][protocol];
	if(!ops) {
		return -ENOTSUP;
	}
	struct file *file = file_create(NULL, FDT_SOCK);
	file->flags |= F_READ | F_WRITE;
	int fd = process_allocate_fd(file, 0);
	if(fd < 0) {
		kobj_putref(file);
		return -EMFILE;
	}
	process_create_proc_fd(current_thread->process, fd, "[socket]");
	struct socket *sock = file->devdata;
	sock->domain = domain;
	sock->type = type;
	sock->protocol = protocol;
	sock->ops = ops;
	if(sock->ops->init) sock->ops->init(sock);
	kobj_putref(file);
	return fd;
}

sysret_t sys_socketpair(int domain, int type, int protocol, int *sv)
{
	protocol = protocol == 0 ? default_protocol[domain][type] : protocol;
	struct sock_calls *ops = domains[domain][protocol];
	if(!ops)
		return -ENOTSUP;
	struct file *f1 = file_create(NULL, FDT_SOCK);
	struct file *f2 = file_create(NULL, FDT_SOCK);

	f1->flags |= F_READ | F_WRITE;
	f2->flags |= F_READ | F_WRITE;
	int fd1 = process_allocate_fd(f1, 0);
	if(fd1 < 0) {
		kobj_putref(f1);
		kobj_putref(f2);
		return -EMFILE;
	}
	process_create_proc_fd(current_thread->process, fd1, "[socket]");
	int fd2 = process_allocate_fd(f2, 0);
	if(fd2 < 0) {
		kobj_putref(f1);
		kobj_putref(f2);
		process_release_fd(fd1);
		return -EMFILE;
	}
	process_create_proc_fd(current_thread->process, fd2, "[socket]");

	struct socket *s1 = f1->devdata;
	struct socket *s2 = f2->devdata;
	s1->domain = s2->domain = domain;
	s1->type = s2->type = type;
	s2->protocol = s1->protocol = protocol;

	s1->ops = s2->ops = ops;
	if(s1->ops->init) s1->ops->init(s1);
	if(s2->ops->init) s2->ops->init(s2);
	s1->ops->sockpair(s1, s2);
	kobj_putref(f1);
	kobj_putref(f2);

	sv[0] = fd1;
	sv[1] = fd2;

	return 0;
}

sysret_t sys_getsockopt(int sockfd, int level, int option, void * restrict value, socklen_t * restrict optlen)
{
	int err = -EBADF;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	spinlock_acquire(&socket->optlock);
	struct sockoptkey key = {.level = level, .option = option};
	struct sockopt *so = hash_lookup(&socket->options, &key, sizeof(key));
	if(so == NULL) {
		err = -ENOPROTOOPT;
	} else {
		err = 0;
		*optlen = *optlen > so->len ? so->len : *optlen;
		memcpy(value, so->data, *optlen);
	}
	spinlock_release(&socket->optlock);
	kobj_putref(socket);
	return 0;
}

sysret_t sys_setsockopt(int sockfd, int level, int option, const void *value, socklen_t optlen)
{
	if(value == NULL)
		return -EINVAL;

	int err = -EBADF;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;

	spinlock_acquire(&socket->optlock);

	if(level == 1) {
		switch(option) {
			case SO_BINDTODEVICE:
				socket->nic = net_nic_get_byname(value);
				if(!socket->nic) {
					spinlock_release(&socket->optlock);
					kobj_putref(socket);
					return -ENOENT;
				}

			case 4:
				spinlock_release(&socket->optlock);
				kobj_putref(socket);
				return 0;
				break;
		}
	} else {
		/* TODO notify option change to layers */
	}

	/* TODO: do we actually need to keep track of options this way? */
	struct sockopt *so = arena_allocate(&socket->optarena, sizeof(struct sockopt) + optlen);
	so->key.level = level;
	so->key.option = option;
	so->len = optlen;
	memcpy(so->data, value, optlen);
	hash_delete(&socket->options, &so->key, sizeof(so->key));
	hash_insert(&socket->options, &so->key, sizeof(so->key), &so->elem, so);
	spinlock_release(&socket->optlock);
	kobj_putref(socket);
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
	printk(":: %p %d\n", socket->ops->connect, err);

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

sysret_t sys_getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;
	
	if(!addr || !addrlen) {
		err = -EINVAL;
	} else if(!(socket->flags & SF_BOUND)) {
		err = -EINVAL;
	} else {
		size_t len = sockaddrinfo[socket->domain].length;
		socklen_t min = *addrlen > len ? len : *addrlen;
		memcpy(addr, &socket->binding, min);
		*addrlen = len;
		err = 0;
	}

	kobj_putref(socket);
	return err;
}

sysret_t sys_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;
	
	if(!addr || !addrlen) {
		err = -EINVAL;
	} else if(!(socket->flags & SF_CONNEC)) {
		err = -ENOTCONN;
	} else {
		size_t len = sockaddrinfo[socket->domain].length;
		socklen_t min = *addrlen > len ? len : *addrlen;
		memcpy(addr, &socket->peer, min);
		*addrlen = len;
		err = 0;
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
	if(sock->flags & SF_SHUTDOWN) {
		thread_send_signal(current_thread, SIGPIPE);
		return -EPIPE;
	}
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

	ssize_t ret = _do_recv(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0));
	kobj_putref(socket);
	return ret;
}

sysret_t sys_send(int sockfd, const char *buf, size_t len, int flags)
{
	int err = -ENOTSUP;
	struct socket *socket = socket_get_from_fd(sockfd, &err);
	if(!socket) return err;
	size_t ret = _do_send(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0));
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
		ret = _do_recv(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0));
	else if(socket->ops->recvfrom)
		ret = socket->ops->recvfrom(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0), src, srclen);
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
		ret = _do_send(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0));
	else if(socket->ops->sendto)
		ret = socket->ops->sendto(socket, buf, len, flags | ((err & O_NONBLOCK) ? _MSG_NONBLOCK : 0), dest, addrlen);
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
	size_t ret = _do_recv(sock, buf, len, (file->flags & O_NONBLOCK) ? _MSG_NONBLOCK : 0);
	kobj_putref(sock);
	return ret;
}

static ssize_t _socket_write(struct file *file, size_t off, size_t len, const char *buf)
{
	(void)off;
	assert(file->devtype == FDT_SOCK);
	struct socket *sock = kobj_getref(file->devdata);
	ssize_t ret = _do_send(sock, buf, len, (file->flags & O_NONBLOCK) ? _MSG_NONBLOCK : 0);
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

#define IFNAMSIZ 16
struct ifreq {
	char ifr_name[IFNAMSIZ]; /* Interface name */
	union {
	    struct sockaddr ifr_addr;
	    struct sockaddr ifr_dstaddr;
	    struct sockaddr ifr_broadaddr;
	    struct sockaddr ifr_netmask;
	    struct sockaddr ifr_hwaddr;
	    short           ifr_flags;
	    int             ifr_ifindex;
	    int             ifr_metric;
	    int             ifr_mtu;
	    char            ifr_slave[IFNAMSIZ];
	    char            ifr_newname[IFNAMSIZ];
	    char           *ifr_data;
	};
};

#define SIOCGIFINDEX 0x8933
static int _socket_ioctl(struct file *file, long cmd, long arg)
{
	struct socket *sock = kobj_getref(file->devdata);
	int ret = 0;
	struct ifreq *ifr = (void *)arg;
	switch(cmd) {
		struct nic *nic = NULL;
		case SIOCGIFINDEX:
			nic = net_nic_get_byname(ifr->ifr_name);
			if(nic) {
				ifr->ifr_ifindex = nic->id;
			} else {
				ret = -ENOENT;
			}
			break;
		default:
			ret = -ENOTSUP;
	}
	kobj_putref(sock);
	return ret;
}

struct file_calls socket_fops = {
	.write = _socket_write,
	.read = _socket_read,
	.create = _socket_fops_create,
	.destroy = _socket_fops_destroy,
	.ioctl = _socket_ioctl, .select = _socket_select, .open = 0, .close = 0,
	.map = 0, .unmap = 0,
};

