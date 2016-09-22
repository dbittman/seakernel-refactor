#include <fs/socket.h>
#include <printk.h>
#include <net/packet.h>
#include <string.h>
#include <errno.h>
#include <thread.h>
#include <blocklist.h>
#include <net/network.h>
#include <file.h>

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

struct tcp_con_key {
	struct sockaddr local, peer;
};

enum tcp_con_state {
	TCS_CLOSED,
	TCS_SYNSENT,
	TCS_SYNRECV,
	TCS_ESTABLISHED,
	TCS_FINWAIT1,
	TCS_FINWAIT2,
	TCS_CLOSING,
	TCS_LASTACK,
	TCS_TIMEWAIT,
};

struct tcp_connection {
	struct kobj_header _header;
	struct hashelem elem;
	struct tcp_con_key key;
	struct socket *local;
	enum tcp_con_state state;
	struct blocklist bl;
	uint32_t seqnum, acknum;
	uint16_t winsize;
};

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

static void _tcp_connection_init(void *o)
{
	struct tcp_connection *c = o;
	c->local = NULL;
	c->state = TCS_CLOSED;
	memset(&c->key, 0, sizeof(c->key));
}

static void _tcp_connection_create(void *o)
{
	struct tcp_connection *c = o;
	blocklist_create(&c->bl);
}

static void _tcp_connection_put(void *o)
{
	struct tcp_connection *c = o;
	if(c->local)
		kobj_putref(c->local);
}

static struct kobj kobj_tcp_connection = {
	KOBJ_DEFAULT_ELEM(tcp_connection),
	.put = _tcp_connection_put, .create = _tcp_connection_create,
	.init = _tcp_connection_init, .destroy = NULL,
};

#define FL_FIN 1
#define FL_SYN (1 << 1)
#define FL_RST (1 << 2)
#define FL_ACK (1 << 4)

int __tcp_send(struct tcp_connection *con, int flags)
{
	struct tcp_header header;
	header.flags = flags;
	header.srcport = *(uint16_t *)con->local->tcp.binding.sa_data;
	header.destport = *(uint16_t *)con->key.peer.sa_data;
	if(FL_ACK) {
		header.seqnum = 0;
		header.acknum = con->acknum;
	} else {
		header.acknum = 0;
		header.seqnum = con->seqnum;
	}
	header.doff = 5;
	header.winsize = HOST_TO_BIG16(con->winsize);
	header.urgptr = 0;

	return net_network_send(con->local, &con->key.peer, &header, header.doff * 4, NULL, 0, PROT_TCP, 16);
}















void tcp_get_ports(struct tcp_header *header, uint16_t *src, uint16_t *dest)
{
	if(src)  *src = header->srcport;
	if(dest) *dest = header->destport;
}

struct socket *_tcp_get_bound_socket(struct sockaddr *addr)
{
	socklen_t len = sockaddrinfo[addr->sa_family].length;
	spinlock_acquire(&bind_lock);
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

void tcp_recv(struct packet *packet, struct tcp_header *header)
{
	if(header->syn && header->ack) {
		printk("GOT SYNACK\n");
	}

	(void)packet;

#if 0
	struct socket *sock = _tcp_get_bound_socket(&packet->daddr);
	if(sock) {
		linkedlist_insert(&sock->tcp.inq, &packet->queue_entry, packet);
		blocklist_unblock_all(&sock->tcp.rbl);
		kobj_putref(sock);
	} else {
		kobj_putref(packet);
	}
#endif

}

static void _tcp_init_sock(struct socket *sock)
{
	sock->tcp.con = NULL;
}

static int _tcp_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	int ret = 0;
	spinlock_acquire(&bind_lock);
	memcpy(&sock->tcp.binding, _addr, len);
	sock->tcp.blen = len;
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

/* TODO: handle peek, OOB */
static ssize_t _tcp_recvfrom(struct socket *sock, char *msg, size_t length,
		int flags, struct sockaddr *src, socklen_t *srclen)
{
	(void)sock;
	(void)msg;
	(void)length;
	(void)flags;
	(void)src;
	(void)srclen;
	return 0;
}

static ssize_t _tcp_sendto(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len)
{
	(void)flags;
	(void)length;
	(void)msg;
	(void)sock;
	if(dest == NULL)
		return -EINVAL;
	if(dest_len < sockaddrinfo[dest->sa_family].length)
		return -EINVAL;
	return 0;
}

static ssize_t _tcp_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	return _tcp_recvfrom(sock, buf, len, flags, NULL, NULL);	
}

static void _tcp_shutdown(struct socket *sock)
{
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

	struct tcp_connection *con = kobj_allocate(&kobj_tcp_connection);
	con->state = TCS_SYNSENT;
	sock->tcp.con = kobj_getref(con);
	con->local = kobj_getref(sock);
	con->seqnum = 1234; //TODO
	con->winsize = 128;
	memcpy(&con->key.peer, addr, alen);
	struct blockpoint bp;
	blockpoint_create(&bp, BLOCK_TIMEOUT, ONE_SECOND * 10 /* TODO */);
	blockpoint_startblock(&con->bl, &bp);
	int ret = __tcp_send(con, FL_SYN);
	if(ret < 0) {
		blockpoint_cleanup(&bp);
		kobj_putref(con);
		kobj_putref(con);
		return ret;
	}
	schedule();

	enum block_result res = blockpoint_cleanup(&bp);
	sock->tcp.con = NULL;
	kobj_putref(con);
	switch(res) {
		case BLOCK_RESULT_INTERRUPTED:
			return -EINTR;
		case BLOCK_RESULT_TIMEOUT:
			return -ETIMEDOUT;
		default: break;
	}

	if(con->state != TCS_ESTABLISHED) {
		kobj_putref(con);
		return -ECONNREFUSED;
	}
	
	if(hash_insert(&connections, &con->key, sizeof(con->key), &con->elem, kobj_getref(con)) != 0) {
		kobj_putref(con);
		kobj_putref(con); //again, since we just got a new reference
		return -EADDRINUSE;
	}

	sock->flags |= SF_CONNEC;
	__tcp_send(con, FL_ACK);
	return 0;
}

struct sock_calls af_tcp_calls = {
	.init = _tcp_init_sock,
	.shutdown = _tcp_shutdown,
	.bind = _tcp_bind,
	.connect = _tcp_connect,
	.listen = NULL,
	.accept = NULL,
	.sockpair = NULL,
	.send = NULL,
	.recv = _tcp_recv,
	.select = _tcp_select,
	.sendto = _tcp_sendto,
	.recvfrom = _tcp_recvfrom,
};

