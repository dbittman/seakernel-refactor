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

#define WINDOWSZ 0x1000

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
	header.srcport = *(uint16_t *)con->local->binding.sa_data;
	header.destport = *(uint16_t *)con->key.peer.sa_data;
	if(FL_ACK) {
		header.acknum = HOST_TO_BIG32(con->recv_next);
	} else {
		header.acknum = 0;
	}
	/* TODO: locking */
	header.seqnum = HOST_TO_BIG32(con->send_next);
	header.doff = 5;
	header.winsize = HOST_TO_BIG16(con->recv_win);
	header.urgptr = 0;
	header._res0 = 0;

	/* if(buf) { */
	/* 	for(size_t i=0;i<len;i++) { */
	/* 		printk("+ %x", (unsigned char)*(buf+i)); */
	/* 	} */
	/* } */

	return net_network_send(con->local, &sock->peer, &header, header.doff * 4, buf, len, PROT_TCP, 16);
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

static void close_connection(struct socket *sock)
{
	sock->tcp.con.state = TCS_CLOSED;
	sock->flags &= ~SF_CONNEC;
	sock->flags |= SF_SHUTDOWN;
	spinlock_acquire(&con_lock);
	struct tcp_con_key *key = &sock->tcp.con.key;
	/* XXX HACK TODO */
	struct sockaddr_in6 *s = (void *)&key->peer;
	struct sockaddr_in6 *d = (void *)&key->local;
	d->scope = 0;
	s->scope = 0;


	if(hash_delete(&connections, key, sizeof(*key)) == 0) {
		kobj_putref(sock);
	}
	spinlock_release(&con_lock);
	blocklist_unblock_all(&sock->pend_con_wait);
	blocklist_unblock_all(&sock->tcp.txbl);
	blocklist_unblock_all(&sock->tcp.rxbl);
	blocklist_unblock_all(&sock->tcp.con.workerbl);
}










static void _tcp_recv_bound_only(struct packet *packet, struct tcp_header *header)
{
	if(header->rst)
		return;
	struct socket *sock = _tcp_get_bound_socket(&packet->daddr);
	if(!sock)
		return;

	switch(sock->tcp.con.state) {
		case TCS_SYNSENT:
			if(header->syn && header->ack) {
				sock->tcp.con.key.local = packet->daddr;
				sock->tcp.con.state = TCS_ESTABLISHED;
				sock->tcp.con.send_next = BIG_TO_HOST32(header->acknum);
				sock->tcp.con.send_unack = sock->tcp.con.send_next;
				sock->tcp.con.recv_next = BIG_TO_HOST32(header->seqnum) + 1;
				sock->tcp.con.send_win = BIG_TO_HOST16(header->winsize);
				blocklist_unblock_all(&sock->tcp.con.bl);
			}
			break;
		case TCS_LISTENING:
			if(header->syn && !header->ack) {
				linkedlist_insert(&sock->pend_con, &packet->queue_entry, kobj_getref(packet));
				blocklist_unblock_all(&sock->pend_con_wait);
			}
			break;
		default: break;
	}
	kobj_putref(sock);
}

static void copy_in_data(struct socket *sock, uint32_t seq, size_t len, char *data)
{
	spinlock_acquire(&sock->tcp.rxlock);
	if((seq >= sock->tcp.con.recv_next && seq < sock->tcp.con.recv_next + sock->tcp.con.recv_win)
		|| ((seq + len - 1) >= sock->tcp.con.recv_next && (seq + len - 1) < sock->tcp.con.recv_next + sock->tcp.con.recv_win)) {
		
		if(seq < sock->tcp.con.recv_next) {
			len -= (sock->tcp.con.recv_next - seq);
			data += (sock->tcp.con.recv_next - seq);
			seq = sock->tcp.con.recv_next;
		}

		if(seq + len >= sock->tcp.con.recv_next + sock->tcp.con.recv_win) {
			len = (sock->tcp.con.recv_next + sock->tcp.con.recv_win) - (seq);
		}

		for(size_t i=0;i<len;i++) {
			sock->tcp.rxbuffer[sock->tcp.con.recv_next++ % WINDOWSZ] = *data++;
		}

		printk("copied in %ld bytes\n", len);
		sock->tcp.con.recv_win -= len;
		blocklist_unblock_all(&sock->tcp.rxbl);
	}
	spinlock_release(&sock->tcp.rxlock);
}

void tcp_recv(struct packet *packet, struct tcp_header *header, size_t len)
{
	struct tcp_con_key key = { .peer = packet->saddr, .local = packet->daddr };
	struct socket *sock = _tcp_get_connection(&key);
	if(!sock) {
		_tcp_recv_bound_only(packet, header);
		return;
	}
	if(header->rst) {
		sock->tcp.con.state = TCS_CLOSED;
		close_connection(sock);
		return;
	}
	switch(sock->tcp.con.state) {
		case TCS_FINWAIT1:
			if(header->fin) {
				sock->tcp.con.state = TCS_CLOSED;
				close_connection(sock);
			}
			break;
		case TCS_ESTABLISHED:
			if(header->ack && !header->syn) {
				spinlock_acquire(&sock->tcp.txlock);
				printk("recv packet: %d %d %ld\n", BIG_TO_HOST32(header->acknum), BIG_TO_HOST32(header->seqnum), len - header->doff * 4);
				sock->tcp.con.send_unack = BIG_TO_HOST32(header->acknum); //TODO: validate
				size_t avail;
				if(sock->tcp.con.send_next >= sock->tcp.con.send_unack)
					avail = sock->tcp.con.send_next - sock->tcp.con.send_unack;
				else
					avail = (0x100000000ull - sock->tcp.con.send_next) + sock->tcp.con.send_unack;
				sock->tcp.txbufavail = (WINDOWSZ - avail) - sock->tcp.pending;
				uint16_t old = sock->tcp.con.send_win;
				sock->tcp.con.send_win = BIG_TO_HOST16(header->winsize);
				if(old != sock->tcp.con.send_win) {
					blocklist_unblock_all(&sock->tcp.con.workerbl);
				}
				spinlock_release(&sock->tcp.txlock);
				if((len - header->doff * 4) > 0) {
					copy_in_data(sock, BIG_TO_HOST32(header->seqnum), len - header->doff * 4, (char *)header->payload + (header->doff - 5)*4);
					__tcp_sendmsg(sock, FL_ACK, 0, 0);
				}
			}
			if(header->fin) {
				printk("Closing...\n");
				sock->tcp.con.state = TCS_LASTACK;
				__tcp_sendmsg(sock, FL_FIN | FL_RST, 0, 0);
				sock->tcp.con.state = TCS_CLOSED;
				close_connection(sock);
				printk("Closed\n");
			}
			break;
		case TCS_SYNRECV:
			if(header->ack && !header->syn) {
				struct socket *bound = _tcp_get_bound_socket(&packet->daddr);
				if(bound) {
					sock->tcp.con.state = TCS_ESTABLISHED;
					sock->tcp.con.recv_next = BIG_TO_HOST32(header->seqnum);
					sock->tcp.con.send_next = BIG_TO_HOST32(header->acknum);
					sock->tcp.con.send_unack = sock->tcp.con.send_next;
					sock->tcp.con.send_win = BIG_TO_HOST16(header->winsize);
					linkedlist_insert(&bound->tcp.establishing, &sock->pend_con_entry, kobj_getref(sock));
					blocklist_unblock_all(&bound->pend_con_wait);
					kobj_putref(bound);
				}
			}
			if(header->fin) {
				sock->tcp.con.state = TCS_LASTACK;
				__tcp_sendmsg(sock, FL_ACK | FL_RST, 0, 0);
			}
			break;
		case TCS_LASTACK:
			if(header->ack) {
				sock->tcp.con.state = TCS_CLOSED;
				close_connection(sock);
			}
			break;
		default: break;
	}

	kobj_putref(sock);
	/* TODO: notify dropped if dropped? */
}










static void _tcp_worker(struct worker *w)
{
	struct socket *sock = worker_arg(w);
	while(worker_notjoining(w)) {
		if((sock->tcp.con.state == TCS_ESTABLISHED && sock->tcp.pending > 0 && sock->tcp.con.send_win > 0) || sock->tcp.con.state == TCS_SYNRECV) {
			spinlock_acquire(&sock->tcp.rxlock);
			spinlock_acquire(&sock->tcp.txlock);

			size_t len = sock->tcp.pending;
			if(len > WINDOWSZ - (sock->tcp.con.send_next % WINDOWSZ))
				len = WINDOWSZ - (sock->tcp.con.send_next % WINDOWSZ);

			if(len > sock->tcp.con.send_win)
				len = sock->tcp.con.send_win;

			printk("%ld send: %d %d %ld: %d\n", current_thread->tid, sock->tcp.con.send_next, sock->tcp.con.recv_next, len, sock->tcp.con.send_next % WINDOWSZ);
			__tcp_sendmsg(sock, FL_ACK, (char *)sock->tcp.txbuffer + (sock->tcp.con.send_next % WINDOWSZ), len);

			sock->tcp.con.send_next += len;
			sock->tcp.pending -= len;

			blocklist_unblock_all(&sock->tcp.txbl);

			spinlock_release(&sock->tcp.txlock);
			spinlock_release(&sock->tcp.rxlock);
		}

		if(sock->tcp.pending == 0 || sock->tcp.con.send_win == 0) {
			printk("%ld Sleeping...\n", current_thread->tid);
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->tcp.con.workerbl, &bp);
			if(sock->tcp.pending == 0 || sock->tcp.con.send_win == 0) {
				printk("%ld Sched\n", current_thread->tid);
				schedule();
				printk("%ld Here\n", current_thread->tid);
			}
			enum block_result res = blockpoint_cleanup(&bp);
			printk("%ld Wake! %d\n", current_thread->tid, res);
		}
	}
	kobj_putref(sock);
	printk("Worker exit\n");
	worker_exit(w, 0);
}

static void _tcp_init_sock(struct socket *sock)
{
	memset(&sock->tcp, 0, sizeof(&sock->tcp));
	sock->tcp.con.state = TCS_CLOSED;
	blocklist_create(&sock->tcp.con.bl);
	blocklist_create(&sock->tcp.con.workerbl);
	linkedlist_create(&sock->tcp.establishing, 0);
	spinlock_create(&sock->tcp.txlock);
	spinlock_create(&sock->tcp.rxlock);
	sock->tcp.txbuffer = (void *)mm_virtual_allocate(WINDOWSZ, false);
	sock->tcp.rxbuffer = (void *)mm_virtual_allocate(WINDOWSZ, false);
	sock->tcp.con.recv_win = WINDOWSZ;
	sock->tcp.con.send_win = 0;
	sock->tcp.txbufavail = WINDOWSZ;
	sock->tcp.pending = 0;
	blocklist_create(&sock->tcp.txbl);
	blocklist_create(&sock->tcp.rxbl);
	worker_start(&sock->tcp.worker, _tcp_worker, kobj_getref(sock));
}

static int _tcp_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	int ret = 0;
	spinlock_acquire(&bind_lock);
	memcpy(&sock->binding, _addr, len);
	sock->tcp.blen = len;

	/* XXX HACK: */
	struct sockaddr_in6 *s6 = (void *)&sock->binding;
	s6->scope = 0;

	if(*(uint16_t *)(sock->binding.sa_data) == 0) {
		*(uint16_t *)sock->binding.sa_data = HOST_TO_BIG16((next_eph++ % 16384) + 49152);
	}
	if(hash_insert(&bindings, &sock->binding, len, &sock->tcp.elem, sock) == -1) {
		ret = -EADDRINUSE;
	} else {
		kobj_getref(sock);
	}
	spinlock_release(&bind_lock);
	return ret;
}

static ssize_t _tcp_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	size_t count = 0;
	spinlock_acquire(&sock->tcp.rxlock);
	while(count == 0) {
		if(sock->tcp.con.state != TCS_ESTABLISHED) {
			spinlock_release(&sock->tcp.rxlock);
			return count > 0 ? (ssize_t)count : -ENOTCONN; //TODO: EINPROGRESS, etc
		}
		size_t pending = WINDOWSZ - sock->tcp.con.recv_win;
		size_t start = sock->tcp.con.recv_next - pending;
		for(;count < len && pending > 0;count++, pending--) {
			*buf++ = sock->tcp.rxbuffer[start++ % WINDOWSZ];
			sock->tcp.con.recv_win++;
		}
		
		if(count == 0) {
			if(flags & _MSG_NONBLOCK) {
				spinlock_release(&sock->tcp.rxlock);
				return -EAGAIN;
			}
			struct blockpoint bp;
			spinlock_release(&sock->tcp.rxlock);
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->tcp.rxbl, &bp);
			if(sock->tcp.con.recv_win == WINDOWSZ) {
				schedule();
			}
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return count == 0 ? -EINTR : (ssize_t)count;
			}
			spinlock_acquire(&sock->tcp.rxlock);
		}
	}
	spinlock_release(&sock->tcp.rxlock);
	return count;
}

static ssize_t _tcp_send(struct socket *sock, const char *buf, size_t len, int flags)
{
	printk("SEND: %d\n", sock->tcp.con.state);
	if(sock->tcp.con.state == TCS_CLOSED)
		return -ENOTCONN;
	if(sock->tcp.con.state != TCS_ESTABLISHED)
		return -EINPROGRESS; //TODO: return proper errors for states
	size_t count = 0;
	spinlock_acquire(&sock->tcp.txlock);
	while(len > 0) {
		if(sock->tcp.con.state != TCS_ESTABLISHED) {
			spinlock_release(&sock->tcp.txlock);
			return count > 0 ? (ssize_t)count : -ENOTCONN; //TODO: EINPROGRESS, etc
		}
		size_t min = len > sock->tcp.txbufavail ? sock->tcp.txbufavail : len;
		size_t start = sock->tcp.con.send_next + sock->tcp.pending;
		printk("Writing: %ld\n", (start) % WINDOWSZ);
		for(size_t i=0;i<min;i++) {
			sock->tcp.txbuffer[(i + start) % WINDOWSZ] = *buf++;
			count++;
			len--;
			sock->tcp.pending++;
			sock->tcp.txbufavail--;
		}
		printk(" Enqueued %ld bytes\n", min);
		blocklist_unblock_all(&sock->tcp.con.workerbl);
		printk("Woke up thread %ld, %ld rem\n", sock->tcp.worker.thread->tid, len);
		if(len > 0) {
			if(flags & _MSG_NONBLOCK) {
				spinlock_release(&sock->tcp.txlock);
				return count == 0 ? -EAGAIN : (ssize_t)count;
			}
			struct blockpoint bp;
			spinlock_release(&sock->tcp.txlock);
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&sock->tcp.txbl, &bp);
			if(sock->tcp.txbufavail == 0) {
				schedule();
			}
			if(blockpoint_cleanup(&bp) == BLOCK_RESULT_INTERRUPTED) {
				return count == 0 ? -EINTR : (ssize_t)count;
			}
			spinlock_acquire(&sock->tcp.txlock);
		}
	}

	spinlock_release(&sock->tcp.txlock);
	return count;
}

#include <interrupt.h>
static void _tcp_shutdown(struct socket *sock)
{
	/* TODO: flush */
	while(!worker_join(&sock->tcp.worker)) {
		blocklist_unblock_all(&sock->tcp.con.workerbl);
		schedule();
	}
	mm_virtual_deallocate((uintptr_t)sock->tcp.txbuffer);
	mm_virtual_deallocate((uintptr_t)sock->tcp.rxbuffer);
	/* remove cons, reset & destroy charbuffer */
	if(sock->tcp.con.state == TCS_ESTABLISHED) {
		sock->tcp.con.state = TCS_FINWAIT1;
		__tcp_sendmsg(sock, FL_FIN | FL_ACK, 0, 0);
	}
	if(sock->flags & SF_BOUND) {
		spinlock_acquire(&bind_lock);
		if(hash_delete(&bindings, &sock->binding, sock->tcp.blen) == -1) {
			panic(0, "bound socket not present in bindings");
		}
		kobj_putref(sock);
		spinlock_release(&bind_lock);
	}
}

static int _tcp_select(struct socket *sock, int flags, struct blockpoint *bp)
{
	//printk(":: Select: %d %d\n", flags, sock->flags);
	if(flags == SEL_ERROR)
		return -1; //TODO

	if(sock->flags & SF_SHUTDOWN)
		return -1;

	if(flags == SEL_READ) {
		if(sock->flags & SF_CONNEC) {
			if(bp)
				blockpoint_startblock(&sock->tcp.rxbl, bp);
			size_t pending = WINDOWSZ - sock->tcp.con.recv_win;
	//		printk("PEND: %ld\n", pending);
			return pending > 0 ? 1 : 0;
		} else if(sock->flags & SF_BOUND) {
			if(bp)
				blockpoint_startblock(&sock->pend_con_wait, bp);
			if(sock->pend_con.count > 0)
				return 1;
			if(sock->tcp.establishing.count > 0)
				return 1;
			return 0;
		} else {
			return -1;
		}
	} else {
		if(bp)
			blockpoint_startblock(&sock->tcp.txbl, bp);
		return sock->tcp.txbufavail > 0 ? 1 : 0;
	}
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
	con->send_next = 0; //TODO
	con->send_unack = 0; //TODO
	memset(&con->key, 0, sizeof(con->key));
	memcpy(&con->key.peer, addr, alen);
	sock->peer = *addr;
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
	printk("Resp OK\n");
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
	con->send_next = 0; //TODO
	con->send_unack = 0; //TODO
	con->recv_next = BIG_TO_HOST32(header->seqnum) + 1;
	memset(&con->key, 0, sizeof(con->key));
	con->key.peer = packet->saddr;
	client->peer = packet->saddr;
	con->key.local = packet->daddr;
	client->binding = sock->binding;
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
				memcpy(addr, &client->peer, minlen);
				*addrlen = minlen;
			}
			kobj_putref(client);
			return ret;
		}
	}
}

static ssize_t _tcp_sendto(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len)
{
	(void)sock; (void)msg; (void)length; (void)flags; (void)dest; (void)dest_len;
	return -ENOTCONN;
}

int _tcp_listen(struct socket *sock, int bl)
{
	sock->tcp.con.state = TCS_LISTENING;
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
	.sendto = _tcp_sendto,
	.recvfrom = NULL,
};

