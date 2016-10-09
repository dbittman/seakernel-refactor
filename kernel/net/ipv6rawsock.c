#include <fs/socket.h>
#include <printk.h>
#include <net/packet.h>
#include <string.h>
#include <errno.h>
#include <thread.h>
#include <blocklist.h>
#include <net/network.h>
#include <file.h>
#include <net/ipv6.h>

static struct linkedlist socks;
static struct spinlock lock;

struct place {
	struct kobj_header _header;
	struct linkedentry entry;
	struct packet *packet;
};

static void _place_put(void *o)
{
	struct place *p = o;
	kobj_putref(p->packet);
}

static struct kobj kobj_place = {
	KOBJ_DEFAULT_ELEM(place),
	.init = NULL, .create = NULL, .put = _place_put, .destroy = NULL,
};

__initializer static void _ipv6_init(void)
{
	spinlock_create(&lock);
	linkedlist_create(&socks, LINKEDLIST_LOCKLESS);
}

static void _ipv6_init_sock(struct socket *sock)
{
	linkedlist_create(&sock->ipv6.inq, 0);
	blocklist_create(&sock->ipv6.rbl);
	spinlock_acquire(&lock);
	linkedlist_insert(&socks, &sock->ipv6.entry, kobj_getref(sock));
	spinlock_release(&lock);
}

/* TODO: put this in another thread to avoid interrupting packet processing */
void ipv6_rawsocket_copy(const struct packet *_packet, struct ipv6_header *header)
{
	if(!socks.count)
		return;
	/* TODO: only do the copy on the first socket that we find. */
	struct packet *packet = packet_duplicate(_packet);
	spinlock_acquire(&lock);
	for(struct linkedentry *entry = linkedlist_iter_start(&socks);
			entry != linkedlist_iter_end(&socks); entry = linkedlist_iter_next(entry)) {
		struct socket *sock = linkedentry_obj(entry);

		/* TODO: ipv6 optional headers break this */
		if(sock->protocol == header->next_header || sock->protocol == 0 || sock->protocol == 255) {
			struct place *place = kobj_allocate(&kobj_place);
			place->packet = kobj_getref(packet);
			linkedlist_insert(&sock->ipv6.inq, &place->entry, place);
			blocklist_unblock_all(&sock->ipv6.rbl);
		}
	}
	spinlock_release(&lock);
	kobj_putref(packet);
}

/* TODO: handle peek, OOB */
/* TODO: can we consoledate this to a all same-type sockets use similar code? */
static ssize_t _ipv6_recvfrom(struct socket *sock, char *msg, size_t length,
		int flags, struct sockaddr *src, socklen_t *srclen)
{
	struct place *place = NULL;
	do {
		place = linkedlist_remove_tail(&sock->ipv6.inq);
		if(place == NULL) {
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->ipv6.rbl, &bp);
			if(sock->ipv6.inq.count == 0) {
				schedule();
			}
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return -EINTR;
			}
		}
	} while(place == NULL && !(flags & _MSG_NONBLOCK));
	if(place == NULL) {
		return -EAGAIN;
	}

	struct ipv6_header *header = place->packet->network_header;
	size_t plen = BIG_TO_HOST16(header->length);// + sizeof(struct ipv6_header); //TODO: copy header?
	size_t minlen = length > plen ? plen : length;
	memcpy(msg, header->data /* header */, minlen);
	if(srclen && src) {
		if(*srclen > sockaddrinfo[place->packet->saddr.sa_family].length)
			*srclen = sockaddrinfo[place->packet->saddr.sa_family].length;
		memcpy(src, &place->packet->saddr, *srclen);
	}

	kobj_putref(place->packet);
	return minlen;
}

static ssize_t _ipv6_sendto(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len)
{
	(void)flags;
	if(dest == NULL)
		return -EINVAL;
	if(dest_len < sockaddrinfo[dest->sa_family].length)
		return -EINVAL;

	int cs = -1;

	spinlock_acquire(&sock->optlock);
	struct sockoptkey key = { .level = SOL_RAW, .option = IPV6_CHECKSUM };
	struct sockopt *so = hash_lookup(&sock->options, &key, sizeof(key));
	if(so && so->len == sizeof(int)) {
		cs = *(int *)so->data;
	}
	spinlock_release(&sock->optlock);
	int err = ipv6_network_send(dest, sock->nic ? kobj_getref(sock->nic) : NULL, NULL, 0, msg, length, sock->protocol, cs);
	return err == 0 ? (ssize_t)length : err;
}

static void _ipv6_shutdown(struct socket *sock)
{
	spinlock_acquire(&lock);
	linkedlist_remove(&socks, &sock->ipv6.entry);
	kobj_putref(sock);
	spinlock_release(&lock);
}

static bool _ipv6_poll(struct socket *sock, struct pollpoint *point)
{
	point->events &= POLLIN | POLLOUT;

	bool ready = false;
	if(point->events & POLLIN) {
		blockpoint_startblock(&sock->ipv6.rbl, &point->bps[POLL_BLOCK_READ]);
		if(sock->ipv6.inq.count > 0) {
			*point->revents |= POLLIN;
			ready = true;
		}
	}
	if(point->events & POLLOUT) {
		point->events &= ~POLLOUT;
		*point->revents |= POLLOUT;
		ready = true;
	}
	return ready;
}

struct sock_calls af_ipv6_calls = {
	.init = _ipv6_init_sock,
	.shutdown = _ipv6_shutdown,
	.bind = NULL,
	.connect = NULL,
	.listen = NULL,
	.accept = NULL,
	.sockpair = NULL,
	.send = NULL,
	.recv = NULL,
	.poll = _ipv6_poll,
	.sendto = _ipv6_sendto,
	.recvfrom = _ipv6_recvfrom,
};

