#include <fs/socket.h>
#include <printk.h>
#include <net/packet.h>
#include <string.h>
#include <errno.h>
#include <thread.h>
#include <blocklist.h>
#include <net/network.h>
#include <file.h>
#include <fs/sys.h>
#include <charbuffer.h>

static struct hash bindings;
static struct hash connections;
static struct spinlock bind_lock;
static struct spinlock con_lock;

static _Atomic uint16_t next_eph = 0;

__initializer static void _tcp_init(void)
{
	hash_create(&bindings, HASH_LOCKLESS, 1024);
	spinlock_create(&bind_lock);

	hash_create(&connections, HASH_LOCKLESS, 1024);
	spinlock_create(&con_lock);
}

struct tcp_header {
	uint16_t srcport;
	uint16_t destport;
	uint32_t seqnum;
	uint32_t acknum;
	uint8_t _res0:4;
	uint8_t doff:4;
	union {
		struct {
			uint8_t fin:1;
			uint8_t syn:1;
			uint8_t rst:1;
			uint8_t psh:1;
			uint8_t ack:1;
			uint8_t urg:1;
			uint8_t ece:1;
			uint8_t cwr:1;
		};
		uint8_t flags;
	};

	uint16_t winsize;
	uint16_t checksum;
	uint16_t urgptr;
	uint8_t payload[];
} __attribute__((packed));

#define FL_FIN 1
#define FL_SYN (1 << 1)
#define FL_RST (1 << 2)
#define FL_ACK (1 << 4)

int __tcp_sendmsg(struct socket *sock, int flags, const char *buf, size_t len)
{
	struct tcp_connection *con = &sock->tcp.con;
	struct tcp_header header;
	header.flags = flags;
	header.srcport = *(uint16_t *)con->local->tcp.binding.sa_data;
	header.destport = *(uint16_t *)con->key.peer.sa_data;
	if(FL_ACK) {
		header.acknum = HOST_TO_BIG32(con->acknum);
	} else {
		header.acknum = 0;
	}
	header.seqnum = HOST_TO_BIG32(con->seqnum);
	header.doff = 5;
	header.winsize = HOST_TO_BIG16(charbuffer_avail(&sock->tcp.inbuf));
	header.urgptr = 0;

	return net_network_send(con->local, &con->peer, &header, header.doff * 4, buf, len, PROT_TCP, 16);
}

#define __tcp_send(s,f) __tcp_sendmsg(s,f,NULL,0)














void tcp_get_ports(struct tcp_header *header, uint16_t *src, uint16_t *dest)
{
	if(src)  *src = header->srcport;
	if(dest) *dest = header->destport;
}

struct socket *_tcp_get_bound_socket(struct sockaddr *addr)
{
	socklen_t len = sockaddrinfo[addr->sa_family].length;
	spinlock_acquire(&bind_lock);
	
	/* XXX HACK: */
	struct sockaddr_in6 *s6 = (void *)addr;
	s6->scope = 0;

	struct socket *sock = hash_lookup(&bindings, addr, len);
	if(sock == NULL) {
		/* check 'any' address */
		struct sockaddr any;
		memcpy(&any, sockaddrinfo[addr->sa_family].any_address, len);
		*(uint16_t *)(any.sa_data) = *(uint16_t *)(addr->sa_data);
		sock = hash_lookup(&bindings, &any, len);
		if(sock == NULL) {
			spinlock_release(&bind_lock);
			return NULL;
		}
	}
	kobj_getref(sock);
	spinlock_release(&bind_lock);
	return sock;
}

static int _tcp_connection_add(struct socket *sock)
{
	struct tcp_connection *con = &sock->tcp.con;
	spinlock_acquire(&con_lock);

	/* XXX HACK TODO */
	struct sockaddr_in6 *s = (void *)&con->key.peer;
	struct sockaddr_in6 *d = (void *)&con->key.local;
	d->scope = 0;
	s->scope = 0;

	if(hash_insert(&connections, &con->key, sizeof(con->key), &con->elem, kobj_getref(sock)) != 0) {
		spinlock_release(&con_lock);
		return -EADDRINUSE;
	}
	spinlock_release(&con_lock);
	return 0;
}

static struct socket *_tcp_get_connection(struct tcp_con_key *key)
{
	/* XXX HACK TODO */
	struct sockaddr_in6 *s = (void *)&key->peer;
	struct sockaddr_in6 *d = (void *)&key->local;
	d->scope = 0;
	s->scope = 0;


	spinlock_acquire(&con_lock);
	struct socket *sock = hash_lookup(&connections, key, sizeof(*key));
	if(sock)
		kobj_getref(sock);
	spinlock_release(&con_lock);
	return sock;
}

void tcp_recv(struct packet *packet, struct tcp_header *header, size_t len)
{
	if(header->syn && header->ack) {
		struct socket *sock = _tcp_get_bound_socket(&packet->daddr);
		/* TODO: thread safe? */
		if(sock && !(sock->flags & SF_CONNEC) && sock->tcp.con.state == TCS_SYNSENT) {
			sock->tcp.con.key.local = packet->daddr;
			sock->tcp.con.state = TCS_ESTABLISHED;
			sock->tcp.con.seqnum = BIG_TO_HOST32(header->acknum);
			sock->tcp.con.acknum = BIG_TO_HOST32(header->seqnum) + 1;
			sock->tcp.con.sndws = BIG_TO_HOST16(header->winsize);
			blocklist_unblock_all(&sock->tcp.con.bl);
		}
		if(sock) kobj_putref(sock);
	} else if(header->syn) {
		struct socket *sock = _tcp_get_bound_socket(&packet->daddr);
		if(sock && sock->tcp.con.state == TCS_LISTENING) {
			linkedlist_insert(&sock->pend_con, &packet->queue_entry, kobj_getref(packet));
			blocklist_unblock_all(&sock->pend_con_wait);
		}
		if(sock) kobj_putref(sock);
	} else {
		/* TODO: locking? */
		size_t datalen = len - header->doff * 4;
		struct tcp_con_key key = { .peer = packet->saddr, .local = packet->daddr };
		struct socket *sock = _tcp_get_connection(&key);
		if(sock) {
			switch(sock->tcp.con.state) {
				case TCS_SYNRECV:
				{
					/* TODO: verify correctness (seqnum, etc), and ACK set */
					struct socket *bound = _tcp_get_bound_socket(&packet->daddr);
					if(bound) {
						sock->tcp.con.state = TCS_ESTABLISHED;
						sock->tcp.con.acknum = BIG_TO_HOST32(header->seqnum);
						sock->tcp.con.seqnum = BIG_TO_HOST32(header->acknum);
						linkedlist_insert(&bound->tcp.establishing, &sock->pend_con_entry, kobj_getref(sock));
						blocklist_unblock_all(&bound->pend_con_wait);
						kobj_putref(bound);
					}
				} /* fall through */
				case TCS_ESTABLISHED:
					sock->tcp.con.seqnum = BIG_TO_HOST32(header->acknum);
					if(sock->tcp.con.acknum == BIG_TO_HOST32(header->seqnum)) {
						if(datalen > 0) {
							ssize_t r = charbuffer_write(&sock->tcp.inbuf, (char *)header->payload + (header->doff - 5)*4, datalen, CHARBUFFER_DO_NONBLOCK);
							if(r > 0) {
								sock->tcp.con.acknum += r;
								__tcp_send(sock, FL_ACK);
							}
						}
					}
					break;
				default:
					/* TODO: reply w/ TCP RST */
					break;
			}
			kobj_putref(sock);
		}
	}
	kobj_putref(packet);
}

static void _tcp_init_sock(struct socket *sock)
{
	sock->tcp.con.state = TCS_CLOSED;
	blocklist_create(&sock->tcp.con.bl);
	charbuffer_create(&sock->tcp.inbuf, 0x1000);
	linkedlist_create(&sock->tcp.establishing, 0);
}

static int _tcp_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	int ret = 0;
	spinlock_acquire(&bind_lock);
	memcpy(&sock->tcp.binding, _addr, len);
	sock->tcp.blen = len;

	/* XXX HACK: */
	struct sockaddr_in6 *s6 = (void *)&sock->tcp.binding;
	s6->scope = 0;

	if(*(uint16_t *)(sock->tcp.binding.sa_data) == 0) {
		*(uint16_t *)sock->tcp.binding.sa_data = HOST_TO_BIG16((next_eph++ % 16384) + 49152);
	}
	if(hash_insert(&bindings, &sock->tcp.binding, len, &sock->tcp.elem, sock) == -1) {
		ret = -EADDRINUSE;
	} else {
		kobj_getref(sock);
	}
	spinlock_release(&bind_lock);
	return ret;
}

static ssize_t _tcp_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	return charbuffer_read(&sock->tcp.inbuf, buf, len, CHARBUFFER_DO_ANY | ((flags & _MSG_NONBLOCK) ? CHARBUFFER_DO_NONBLOCK : 0));
}

static ssize_t _tcp_send(struct socket *sock, const char *buf, size_t len, int flags)
{
	(void)flags;
	if(sock->tcp.con.state != TCS_ESTABLISHED)
		return -EINPROGRESS; //TODO: return proper errors for states
	/* TODO: buffering... winsize, etc */
	__tcp_sendmsg(sock, FL_ACK, buf, len);
	return len;
}

static void _tcp_shutdown(struct socket *sock)
{
	/* remove cons, reset & destroy charbuffer */
	spinlock_acquire(&bind_lock);
	if(sock->flags & SF_BOUND) {
		if(hash_delete(&bindings, &sock->tcp.binding, sock->tcp.blen) == -1) {
			panic(0, "bound socket not present in bindings");
		}
		kobj_putref(sock);
	}
	spinlock_release(&bind_lock);
}

static int _tcp_select(struct socket *sock, int flags, struct blockpoint *bp)
{
	(void)flags;
	(void)sock;
	(void)bp;
	return 1; //TODO: don't always indicate ready for writing.
}

static int _tcp_connect(struct socket *sock, const struct sockaddr *addr, socklen_t alen)
{
	/* TODO: thread-safe? (UDP code too?) */
	if(!(sock->flags & SF_BOUND)) {
		if(_tcp_bind(sock, sockaddrinfo[sock->domain].any_address, sockaddrinfo[sock->domain].length) != 0)
			return -ENOMEM; //TODO
		sock->flags |= SF_BOUND;
	}
	struct tcp_connection *con = &sock->tcp.con;
	con->state = TCS_SYNSENT;
	con->local = sock;
	con->seqnum = 0; //TODO
	memset(&con->key, 0, sizeof(con->key));
	memcpy(&con->key.peer, addr, alen);
	con->peer = *addr;
	struct blockpoint bp;
	blockpoint_create(&bp, BLOCK_TIMEOUT, ONE_SECOND * 10 /* TODO */);
	blockpoint_startblock(&con->bl, &bp);
	int ret = __tcp_send(sock, FL_SYN);
	if(ret < 0) {
		blockpoint_cleanup(&bp);
		return ret;
	}
	schedule();

	enum block_result res = blockpoint_cleanup(&bp);
	switch(res) {
		case BLOCK_RESULT_INTERRUPTED:
			return -EINTR;
		case BLOCK_RESULT_TIMEOUT:
			return -ETIMEDOUT;
		default: break;
	}

	if(con->state != TCS_ESTABLISHED) {
		return -ECONNREFUSED;
	}
	
	ret = _tcp_connection_add(sock);
	if(ret < 0)
		return ret;

	sock->flags |= SF_CONNEC;
	__tcp_send(sock, FL_ACK);
	return 0;
}

static int _start_establish(struct socket *sock, struct packet *packet)
{
	int fd = sys_socket(sock->domain, sock->type, sock->protocol);
	if(fd < 0)
		return fd;

	int err;
	struct socket *client = socket_get_from_fd(fd, &err);
	if(!client) {
		sys_close(fd);
		return -err;
	}

	struct tcp_header *header = packet->transport_header;
	struct tcp_connection *con = &client->tcp.con;
	con->state = TCS_SYNRECV;
	con->local = client;
	con->seqnum = 0; //TODO
	con->acknum = BIG_TO_HOST32(header->seqnum) + 1;
	memset(&con->key, 0, sizeof(con->key));
	con->key.peer = packet->saddr;
	con->peer = packet->saddr;
	con->key.local = packet->daddr;
	client->tcp.binding = sock->tcp.binding;
	client->tcp.tmpfd = fd;

	err = _tcp_connection_add(client);
	if(err < 0) {
		//TODO send RST and clean up
		return 0;
	}

	__tcp_send(client, FL_SYN | FL_ACK);
	kobj_putref(client);

	return 0;
}

/* TODO: care about backlog */
static int _tcp_accept(struct socket *sock, struct sockaddr *addr, socklen_t *addrlen)
{
	if(!(sock->flags & SF_BOUND) || !(sock->flags & SF_LISTEN)) {
		return -EINVAL;
	}

	sock->tcp.con.state = TCS_LISTENING;
	while(true) {
		struct packet *packet = linkedlist_remove_tail(&sock->pend_con);
		struct socket *client = linkedlist_remove_tail(&sock->tcp.establishing);
		if(packet == NULL && client == NULL) {
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->pend_con_wait, &bp);
			if(sock->pend_con.count == 0 && sock->tcp.establishing.count == 0) {
				schedule();
			}
			enum block_result res = blockpoint_cleanup(&bp);
			if(res == BLOCK_RESULT_INTERRUPTED)
				return -EINTR;
		}

		if(packet) {
			_start_establish(sock, packet);
			kobj_putref(packet);
		}
		if(client) {
			int ret = client->tcp.tmpfd;
			client->flags |= SF_CONNEC;
			if(addr && addrlen) {
				socklen_t minlen = sockaddrinfo[client->domain].length;
				if(minlen > *addrlen)
					minlen = *addrlen;
				memcpy(addr, &client->tcp.con.peer, minlen);
				*addrlen = minlen;
			}
			kobj_putref(client);
			return ret;
		}
	}
}

int _tcp_listen(struct socket *sock, int bl)
{
	(void)sock;
	(void)bl;
	return 0;
}

struct sock_calls af_tcp_calls = {
	.init = _tcp_init_sock,
	.shutdown = _tcp_shutdown,
	.bind = _tcp_bind,
	.connect = _tcp_connect,
	.listen = _tcp_listen,
	.accept = _tcp_accept,
	.sockpair = NULL,
	.send = _tcp_send,
	.recv = _tcp_recv,
	.select = _tcp_select,
	.sendto = NULL,
	.recvfrom = NULL,
};
