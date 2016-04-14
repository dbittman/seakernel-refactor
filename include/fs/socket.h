#pragma once

#include <lib/hash.h>
#include <charbuffer.h>
#include <blocklist.h>
#include <slab.h>
#include <fs/inode.h>

typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
#define AF_UNIX 1

#define MAX_AF 2

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};

struct socket;
struct sock_calls {
	void (*init)(struct socket *);
	void (*shutdown)(struct socket *);
	int (*connect)(struct socket *, const struct sockaddr *, socklen_t);
	int (*bind)(struct socket *, const struct sockaddr *, socklen_t);
	int (*accept)(struct socket *, struct sockaddr *addr, socklen_t *addrlen);
	int (*listen)(struct socket *, int backlog);
	int (*sockpair)(struct socket *, struct socket *);
	ssize_t (*recv)(struct socket *, char *, size_t, int);
	ssize_t (*send)(struct socket *, const char *, size_t, int);
	ssize_t (*sendto)(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len);
	ssize_t (*recvfrom)(struct socket *, char *, size_t, int, struct sockaddr *, socklen_t *);
};

struct unix_connection {
	struct kobj_header _header;
	struct socket *server, *client;
	struct charbuffer in, out;
	_Atomic bool terminating;
};

struct socket_unix_data {
	bool named;
	int fd;
	struct sockaddr_un name;
	struct inode_id loc;
	struct hashelem elem;
	struct unix_connection * _Atomic con;
	struct unix_connection _condata; // the server socket stores the connection
};

#define SF_BOUND  1
#define SF_LISTEN 2
#define SF_ACCEPT 4
#define SF_CONNEC 8
#define SF_SHUTDOWN 0x10

struct socket {
	struct kobj_header _header;
	int domain, type, protocol;
	_Atomic int flags;
	struct sock_calls *ops;
	int backlog;
	struct linkedlist pend_con;
	struct blocklist pend_con_wait;
	struct linkedentry pend_con_entry;
	union {
		struct socket_unix_data unix;
	};
};

struct socket *socket_get_from_fd(int fd, int *err);

