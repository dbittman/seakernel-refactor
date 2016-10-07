#pragma once

#include <lib/hash.h>
#include <charbuffer.h>
#include <blocklist.h>
#include <slab.h>
#include <fs/inode.h>
#include <arena.h>
#include <worker.h>

typedef unsigned socklen_t;
typedef unsigned short sa_family_t;
typedef uint16_t in_port_t;

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define AF_UNIX   1
#define AF_INET6 10

#define PROT_UDP      17
#define PROT_TCP      6
#define PROT_ICMPV6   58

#define MAX_PROT 58
#define MAX_AF   10
#define MAX_TYPE  3

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[64];
};

struct sockaddrinfo {
	socklen_t length;
	const struct sockaddr *any_address;
};

struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};

union ipv6_address {
	uint8_t octets[16];
	uint32_t u32[4];
	struct {
		uint64_t prefix;
		uint64_t id;
	} __attribute__((packed));
	__int128 addr;
};

struct sockaddr_in6 {
	sa_family_t sa_family;
	in_port_t port;
	uint32_t flow;
	union ipv6_address addr;
	uint32_t scope;
} __attribute__((packed));

extern struct sockaddrinfo sockaddrinfo[MAX_AF + 1];
_Static_assert(sizeof(struct sockaddr) >= sizeof(struct sockaddr_in6), "");

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
	int (*select)(struct socket *, int, struct blockpoint *);
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

struct socket_udp_data {
	struct hashelem elem;
	struct sockaddr binding;
	struct linkedlist inq;
	struct blocklist rbl;
	size_t blen;
};

struct tcp_con_key {
	struct sockaddr local, peer;
};

enum tcp_con_state {
	TCS_CLOSED,
	TCS_LISTENING,
	TCS_SYNSENT,
	TCS_SYNRECV,
	TCS_ESTABLISHED,
	TCS_FINWAIT1,
	TCS_FINWAIT2,
	TCS_CLOSING,
	TCS_LASTACK,
	TCS_TIMEWAIT,
	TCS_FINISHED,
	TCS_RESET,
};

struct tcp_connection {
	struct hashelem elem;
	struct tcp_con_key key;
	struct socket *local;
	_Atomic enum tcp_con_state state;
	struct blocklist bl, workerbl;

	_Atomic uint32_t send_next, send_unack, recv_next, recv_win, send_win;
};

struct socket;
struct socket_tcp_data {
	struct hashelem elem;
	size_t blen;
	struct linkedlist establishing;
	int tmpfd;
	
	struct tcp_connection con;
	uint8_t *txbuffer;
	uint8_t *rxbuffer;
	_Atomic size_t txbufavail, pending;
	struct spinlock txlock;
	struct spinlock rxlock;
	time_t time;
	struct blocklist txbl, rxbl;
	struct worker worker;
};

struct socket_ipv6raw_data {
	struct linkedentry entry;
	struct linkedlist inq;
	struct blocklist rbl;
};

#define SF_BOUND  1
#define SF_LISTEN 2
#define SF_ACCEPT 4
#define SF_CONNEC 8
#define SF_SHUTDOWN 0x10

#define _MSG_NONBLOCK (1u << 31)

//some compilers define 'unix' as 1, which doesn't make sense for kernel code.
#undef unix

#define IPV6_CHECKSUM 7
#define SOL_RAW 255
#define SO_BINDTODEVICE 25

struct sockoptkey {
	int level;
	int option;
};

struct sockopt {
	struct sockoptkey key;
	size_t len;
	struct hashelem elem;
	char data[];
};

struct nic;
struct socket {
	struct kobj_header _header;
	int domain, type, protocol;
	_Atomic int flags;
	struct sock_calls *ops;
	struct sockaddr peer;
	struct sockaddr binding;
	int backlog;
	struct linkedlist pend_con;
	struct blocklist pend_con_wait;
	struct linkedentry pend_con_entry;
	struct hash options;
	struct arena optarena;
	struct spinlock optlock;
	struct nic *nic;
	union {
		struct socket_unix_data unix;
		struct socket_udp_data udp;
		struct socket_tcp_data tcp;
		struct socket_ipv6raw_data ipv6;
	};
};

struct socket *socket_get_from_fd(int fd, int *err);

