#include <fs/socket.h>
#include <printk.h>
#include <net/packet.h>
#include <string.h>
#include <errno.h>
#include <thread.h>
#include <blocklist.h>

static struct hash bindings;
static struct spinlock bind_lock;

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
	(void)sock;
	linkedlist_create(&sock->udp.inq, 0);
	blocklist_create(&sock->udp.rbl);
	printk("UDP INIT\n");
}

void udp_get_ports(struct udp_header *header, uint16_t *src, uint16_t *dest)
{
	if(src)  *src = header->srcport;
	if(dest) *dest = header->destport;
}

struct socket *_udp_get_bound_socket(struct sockaddr *addr)
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

void udp_recv(struct packet *packet, struct udp_header *header)
{
	(void)header;
	printk("UDP recv: %d %d\n", BIG_TO_HOST16(*(uint16_t *)packet->saddr.sa_data), BIG_TO_HOST16(*(uint16_t *)packet->daddr.sa_data));
	struct socket *sock = _udp_get_bound_socket(&packet->daddr);
	if(sock) {
		linkedlist_insert(&sock->udp.inq, &packet->queue_entry, packet);
		blocklist_unblock_all(&sock->udp.rbl);
	} else {
		kobj_putref(packet);
	}
}

static int _udp_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	int ret = 0;
	spinlock_acquire(&bind_lock);
	memcpy(&sock->udp.binding, _addr, len);
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
	}

	kobj_putref(packet);
	return length;
}

struct sock_calls af_udp_calls = {
	.init = _udp_init_sock,
	.shutdown = NULL,
	.bind = _udp_bind,
	.connect = NULL,
	.listen = NULL,
	.accept = NULL,
	.sockpair = NULL,
	.send = NULL,
	.recv = NULL,
	.select = NULL,
	.sendto = NULL,
	.recvfrom = _udp_recvfrom,
};

