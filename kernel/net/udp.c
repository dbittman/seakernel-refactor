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
static struct spinlock bind_lock;

static _Atomic uint16_t next_eph = 0;

__initializer static void _udp_init(void)
{
	hash_create(&bindings, HASH_LOCKLESS, 1024);
	spinlock_create(&bind_lock);
}

struct udp_header {
	uint16_t srcport;
	uint16_t destport;
	uint16_t length;
	uint16_t checksum;
	uint8_t payload[];
};

static void _udp_init_sock(struct socket *sock)
{
	linkedlist_create(&sock->udp.inq, 0);
	blocklist_create(&sock->udp.rbl);
}

void udp_get_ports(struct udp_header *header, uint16_t *src, uint16_t *dest)
{
	if(src)  *src = header->srcport;
	if(dest) *dest = header->destport;
}

struct socket *_udp_get_bound_socket(struct sockaddr *addr)
{
	socklen_t len = sockaddrinfo[addr->sa_family].length;

	/* XXX HACK: */
	struct sockaddr_in6 *s6 = (void *)addr;
	s6->scope = 0;

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

void udp_recv(struct packet *packet, struct udp_header *header)
{
	(void)header;
	struct socket *sock = _udp_get_bound_socket(&packet->daddr);
	if(sock) {
		linkedlist_insert(&sock->udp.inq, &packet->queue_entry, packet);
		blocklist_unblock_all(&sock->udp.rbl);
		kobj_putref(sock);
	} else {
		kobj_putref(packet);
	}
}

static int _udp_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	int ret = 0;
	spinlock_acquire(&bind_lock);
	memcpy(&sock->udp.binding, _addr, len);

	/* XXX HACK: */
	struct sockaddr_in6 *s6 = (void *)&sock->udp.binding;
	s6->scope = 0;

	sock->udp.blen = len;
	if(*(uint16_t *)(sock->udp.binding.sa_data) == 0) {

		*(uint16_t *)sock->udp.binding.sa_data = HOST_TO_BIG16((next_eph++ % 16384) + 49152);
	}
	if(hash_insert(&bindings, &sock->udp.binding, len, &sock->udp.elem, sock) == -1) {
		ret = -EADDRINUSE;
	} else {
		kobj_getref(sock);
	}
	spinlock_release(&bind_lock);
	return ret;
}

/* TODO: handle peek, OOB */
static ssize_t _udp_recvfrom(struct socket *sock, char *msg, size_t length,
		int flags, struct sockaddr *src, socklen_t *srclen)
{
	struct packet *packet;
	do {
		packet = linkedlist_remove_tail(&sock->udp.inq);
		if(packet == NULL) {
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->udp.rbl, &bp);
			if(sock->udp.inq.count == 0) {
				schedule();
			}
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return -EINTR;
			}
		}
	} while(packet == NULL && !(flags & _MSG_NONBLOCK));
	if(packet == NULL) {
		return -EAGAIN;
	}

	struct udp_header *header = packet->transport_header;
	size_t plen = BIG_TO_HOST16(header->length) - 8;
	size_t minlen = length > plen ? plen : length;
	memcpy(msg, header->payload, minlen);
	if(srclen && src) {
		if(*srclen > sockaddrinfo[packet->saddr.sa_family].length)
			*srclen = sockaddrinfo[packet->saddr.sa_family].length;
		memcpy(src, &packet->saddr, *srclen);

		/* XXX HACK: */
		struct sockaddr_in6 *s6 = (void *)src;
		s6->scope = 0;
	}

	kobj_putref(packet);
	return minlen;
}

static ssize_t _udp_sendto(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len)
{
	(void)flags;
	if(dest == NULL)
		return -EINVAL;
	if(dest_len < sockaddrinfo[dest->sa_family].length)
		return -EINVAL;
	if(!(sock->flags & SF_BOUND)) {
		assert(sock->domain == AF_INET6);
		if(_udp_bind(sock, sockaddrinfo[sock->domain].any_address, sockaddrinfo[sock->domain].length) != 0)
			return -ENOMEM; //TODO
		sock->flags |= SF_BOUND;
	}

	struct udp_header header = {.destport = *(uint16_t *)(dest->sa_data), .srcport = *(uint16_t *)(sock->udp.binding.sa_data), .length = HOST_TO_BIG16(length + 8), .checksum = 0};
	int err = net_network_send(sock, dest, &header, 8, msg, length, PROT_UDP, 6);
	return err == 0 ? (ssize_t)length : err;
}

static ssize_t _udp_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	return _udp_recvfrom(sock, buf, len, flags, NULL, NULL);	
}

static void _udp_shutdown(struct socket *sock)
{
	spinlock_acquire(&bind_lock);
	if(sock->flags & SF_BOUND) {
		if(hash_delete(&bindings, &sock->udp.binding, sock->udp.blen) == -1) {
			panic(0, "bound socket not present in bindings");
		}
		kobj_putref(sock);
	}
	spinlock_release(&bind_lock);
}

static int _udp_select(struct socket *sock, int flags, struct blockpoint *bp)
{
	if(flags == SEL_ERROR)
		return -1; //TODO

	if(flags == SEL_READ) {
		if(bp)
			blockpoint_startblock(&sock->udp.rbl, bp);
		return sock->udp.inq.count == 0 ? 0 : 1;
	}
	return 1; //TODO: don't always indicate ready for writing.
}

struct sock_calls af_udp_calls = {
	.init = _udp_init_sock,
	.shutdown = _udp_shutdown,
	.bind = _udp_bind,
	.connect = NULL,
	.listen = NULL,
	.accept = NULL,
	.sockpair = NULL,
	.send = NULL,
	.recv = _udp_recv,
	.select = _udp_select,
	.sendto = _udp_sendto,
	.recvfrom = _udp_recvfrom,
};

