#include <fs/socket.h>
#include <printk.h>
#include <fs/sys.h>
#include <fs/stat.h>
#include <lib/hash.h>
#include <system.h>
#include <string.h>
#include <errno.h>
#include <slab.h>
#include <thread.h>

static struct hash bound_sockets;

__initializer static void _unix_init(void)
{
	hash_create(&bound_sockets, 0, 128);
}

static void create_connection(struct socket *server, struct socket *client)
{
	client->unix.con = &server->unix._condata;
	server->unix.con = client->unix.con;
	client->unix.con->server = server;
	client->unix.con->client = client;
	charbuffer_create(&client->unix.con->in, 0x1000);
	charbuffer_create(&client->unix.con->out, 0x1000);
	server->flags |= SF_CONNEC;
	client->flags |= SF_CONNEC;
}

static ssize_t _unix_send(struct socket *sock, const char *buf, size_t len, int flags)
{
	(void)flags;
	if(sock->unix.con == &sock->unix._condata)
		return charbuffer_write(&sock->unix.con->out, buf, len, 0);
	else
		return charbuffer_write(&sock->unix.con->in, buf, len, 0);
}

static ssize_t _unix_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	(void)flags;
	if(sock->unix.con == &sock->unix._condata)
		return charbuffer_read(&sock->unix.con->in, buf, len, 0);
	else
		return charbuffer_read(&sock->unix.con->out, buf, len, 0);
}

static int _unix_connect(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	(void)len;
	const struct sockaddr_un *addr = (struct sockaddr_un *)_addr;
	struct socket *master = hash_lookup(&bound_sockets, addr->sun_path, strlen(addr->sun_path));
	if(!master)
		return -ENOENT;
	
	struct blockpoint bp;
	blockpoint_create(&bp, 0, 0);
	blockpoint_startblock(&sock->pend_con_wait, &bp);

	linkedlist_insert(&master->pend_con, &sock->pend_con_entry, kobj_getref(sock));
	blocklist_unblock_one(&sock->pend_con_wait);
	
	schedule();
	blockpoint_cleanup(&bp);
	/* TODO: test for failure */
		
	return 0;
}

static int _unix_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	const struct sockaddr_un *addr = (struct sockaddr_un *)_addr;

	int fd = sys_open(addr->sun_path, O_CREAT | O_RDWR | O_EXCL, S_IFSOCK | 0755);
	if(fd < 0)
		return fd == -EBUSY ? -EADDRINUSE : fd;
	sys_close(fd);

	memcpy(&sock->unix.name, addr, len);
	if(hash_insert(&bound_sockets, sock->unix.name.sun_path, strlen(sock->unix.name.sun_path), &sock->unix.elem, kobj_getref(sock)) == -1)
		return -EADDRINUSE;

	return 0;
}

static int _unix_accept(struct socket *sock, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)addr;
	(void)addrlen;
	struct blockpoint bp;
	struct socket *client = NULL;
	while(client == NULL) {
		blockpoint_create(&bp, 0, 0);
		blockpoint_startblock(&sock->pend_con_wait, &bp);

		client = linkedlist_remove_tail(&sock->pend_con);
		if(client)
			blockpoint_unblock(&bp);
		else
			schedule();
		blockpoint_cleanup(&bp);
	}

	int fd = sys_socket(sock->domain, sock->type, sock->protocol);
	if(fd < 0)
		return fd;

	int err;
	struct socket *server = socket_get_from_fd(fd, &err);
	if(!server) {
		sys_close(fd);
		return -err;
	}
	create_connection(server, client);
	
	kobj_putref(server);
	blocklist_unblock_all(&client->pend_con_wait);
	kobj_putref(client);
	return fd;
}

static int _unix_listen(struct socket *sock, int backlog)
{
	(void)sock;
	(void)backlog;
	return 0;
}

static int _unix_sockpair(struct socket *s1, struct socket *s2)
{
	create_connection(s1, s2);
	return 0;
}

struct sock_calls af_unix_calls = {
	.bind = _unix_bind,
	.connect = _unix_connect,
	.listen = _unix_listen,
	.accept = _unix_accept,
	.sockpair = _unix_sockpair,
	.send = _unix_send,
	.recv = _unix_recv,
};

