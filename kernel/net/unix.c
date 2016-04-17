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
#include <file.h>
#include <fs/path.h>

static struct hash bound_sockets;
struct spinlock _id_lock;

static _Atomic long next_id = 0;

__initializer static void _unix_init(void)
{
	hash_create(&bound_sockets, 0, 128);
	spinlock_create(&_id_lock);
}

static void _unix_con_init(void *obj)
{
	struct unix_connection *con = obj;
	con->terminating = false;
	charbuffer_reset(&con->in);
	charbuffer_reset(&con->out);
}

static void _unix_con_create(void *obj)
{
	struct unix_connection *con = obj;
	charbuffer_create(&con->in, 0x1000);
	charbuffer_create(&con->out, 0x1000);
}

static void _unix_con_destroy(void *obj)
{
	struct unix_connection *con = obj;
	charbuffer_destroy(&con->in);
	charbuffer_destroy(&con->out);
}

static struct kobj kobj_unix_connection = {
	KOBJ_DEFAULT_ELEM(unix_connection),
	.create = _unix_con_create,
	.destroy = _unix_con_destroy,
	.init = _unix_con_init, .put = NULL,
};

static void create_connection(struct socket *server, struct socket *client)
{
	struct unix_connection *con = kobj_allocate(&kobj_unix_connection);
	client->unix.con = con;
	server->unix.con = kobj_getref(con);

	client->unix.con->server = kobj_getref(server);
	client->unix.con->client = kobj_getref(client);
	
	client->unix.con->terminating = false;
	server->flags |= SF_CONNEC;
	client->flags |= SF_CONNEC;
}

static void cleanup_connection(struct socket *sock, size_t amount)
{
	if(amount != 0)
		return;
	struct unix_connection *con = sock->unix.con;
	if(!con || !con->terminating)
		return;
	if(atomic_compare_exchange_strong(&sock->unix.con, &con, NULL) && con) {
		sock->flags &= ~SF_CONNEC;
		sock->flags |= SF_SHUTDOWN;
		if(con->server == sock) {
			con->server = NULL;
			kobj_putref(sock);
			kobj_putref(con);
		} else {
			con->client = NULL;
			kobj_putref(sock);
			kobj_putref(con);
		}
	}
}

static void terminate_connection(struct unix_connection *con)
{
	con->terminating = true;
	charbuffer_terminate(&con->out);
	charbuffer_terminate(&con->in);
}

static ssize_t _unix_send(struct socket *sock, const char *buf, size_t len, int flags)
{
	(void)flags; //TODO: support flags
	struct unix_connection *con = sock->unix.con;
	ssize_t ret;
	if(con->server == sock)
		ret = charbuffer_write(&con->out, buf, len, 0);
	else
		ret = charbuffer_write(&con->in, buf, len, 0);
	cleanup_connection(sock, ret);
	return ret;
}

static ssize_t _unix_recv(struct socket *sock, char *buf, size_t len, int flags)
{
	(void)flags;
	ssize_t ret;
	struct unix_connection *con = sock->unix.con;
	if(con->server == sock)
		ret = charbuffer_read(&con->in, buf, len, CHARBUFFER_DO_ANY);
	else
		ret = charbuffer_read(&con->out, buf, len, CHARBUFFER_DO_ANY);
	cleanup_connection(sock, ret);
	return ret;
}

static int _unix_connect(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	(void)len;
	const struct sockaddr_un *addr = (struct sockaddr_un *)_addr;
	struct inode *node;
	int err = fs_path_resolve(addr->sun_path, NULL, 0, 0, NULL, &node);
	if(err < 0)
		return err;
	spinlock_acquire(&_id_lock);
	struct socket *master = hash_lookup(&bound_sockets, &node->id, sizeof(node->id));
	if(master) kobj_getref(master);
	spinlock_release(&_id_lock);
	inode_put(node);
	if(!master)
		return -ENOENT;

	struct blockpoint bp;
	blockpoint_create(&bp, BLOCK_TIMEOUT, ONE_SECOND);
	blockpoint_startblock(&sock->pend_con_wait, &bp);

	linkedlist_insert(&master->pend_con, &sock->pend_con_entry, kobj_getref(sock));
	blocklist_unblock_one(&master->pend_con_wait);
	
	schedule();
	blockpoint_cleanup(&bp);
	if(!(sock->flags & SF_CONNEC)) {
		kobj_putref(master);
		return -ECONNREFUSED;
	}
	sock->unix.named = true;
	sock->unix.name.sun_path[0] = 0;
	long id = ++next_id;
	memcpy(&sock->unix.name.sun_path[sizeof(next_id)], &id, sizeof(id));
	kobj_putref(master);
		
	return 0;
}

static int _unix_bind(struct socket *sock, const struct sockaddr *_addr, socklen_t len)
{
	const struct sockaddr_un *addr = (struct sockaddr_un *)_addr;

	int fd = sys_open(addr->sun_path, O_CREAT | O_RDWR | O_EXCL, S_IFSOCK | 0755);
	if(fd < 0)
		return fd == -EEXIST ? -EADDRINUSE : fd;
	sock->unix.fd = fd;
	memcpy(&sock->unix.name, addr, len);

	struct file *anchor = process_get_file(fd);
	struct inode *node = file_get_inode(anchor);
	memcpy(&sock->unix.loc, &node->id, sizeof(node->id));
	inode_put(node);
	kobj_putref(anchor);

	spinlock_acquire(&_id_lock);
	if(hash_insert(&bound_sockets, &sock->unix.loc, sizeof(sock->unix.loc), &sock->unix.elem, kobj_getref(sock)) == -1) {
		spinlock_release(&_id_lock);
		return -EADDRINUSE;
	}
	spinlock_release(&_id_lock);
	sock->unix.named = true;

	return 0;
}

static int _unix_accept(struct socket *sock, struct sockaddr *addr, socklen_t *addrlen)
{
	(void)addr;
	(void)addrlen;
	struct blockpoint bp;
	struct socket *client = NULL;
	while(client == NULL) {
		if(!(sock->flags & SF_BOUND))
			return -EINVAL;
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
	server->unix.name.sun_path[0] = 0;
	memcpy(&server->unix.name, &sock->unix.name, sizeof(server->unix.name));
	server->unix.named = true;
	create_connection(server, client);
	
	kobj_putref(server);
	blocklist_unblock_all(&client->pend_con_wait);
	memcpy(addr, &client->unix.name, *addrlen);
	kobj_putref(client);
	return fd;
}

/*
static ssize_t _unix_sendto(struct socket *sock, const char *msg, size_t length,
		int flags, const struct sockaddr *dest, socklen_t dest_len)
{
	
}

static ssize_t _unix_recvfrom(struct socket *sock, char *msg, size_t length,
		int flags, struct sockaddr *src, socklen_t *srclen)
{
	(void)msg;
}
*/

static void _unix_shutdown(struct socket *sock)
{
	if(sock->unix.con)
		terminate_connection(sock->unix.con);
	if(sock->flags & SF_BOUND) {
		spinlock_acquire(&_id_lock);
		sock->flags &= ~SF_BOUND;
		hash_delete(&bound_sockets, &sock->unix.loc, sizeof(sock->unix.loc));
		blocklist_unblock_all(&sock->pend_con_wait);
		spinlock_release(&_id_lock);
		sys_close(sock->unix.fd);
	}
	cleanup_connection(sock, 0);
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

static void _unix_init_sock(struct socket *sock)
{
	sock->unix.named = false;
	sock->unix.fd = -1;
}

static int _unix_select(struct socket *sock, int flags, struct blockpoint *bp)
{
	if(flags == SEL_ERROR)
		return -1; // TODO
	if(!(sock->flags & SF_CONNEC)) {
		if((sock->flags & SF_LISTEN) && flags == SEL_READ) {
			if(bp)
				blockpoint_startblock(&sock->pend_con_wait, bp);
			if(sock->pend_con.count > 0) {
				if(bp)
					blockpoint_unblock(bp);
				return 1;
			}
			return 0;
		}
		return 1;
	}

	struct unix_connection *con = sock->unix.con;
	struct charbuffer *buf = NULL;
	if(con->server == sock && flags == SEL_READ)
		buf = &con->in;
	else if(con->server == sock && flags == SEL_WRITE)
		buf = &con->out;
	else if(flags == SEL_WRITE)
		buf = &con->in;
	else if(flags == SEL_READ)
		buf = &con->out;

	assert(buf != NULL);
	if(bp)
		blockpoint_startblock((flags == SEL_READ) ? &buf->wait_read : &buf->wait_write, bp);

	int ret = 0;
	if(flags == SEL_READ) {
		if(charbuffer_pending(buf)) {
			ret = 1;
			if(bp)
				blockpoint_unblock(bp);
		}
	} else if(flags == SEL_WRITE) {
		if(charbuffer_avail(buf)) {
			ret = 1;
			if(bp)
				blockpoint_unblock(bp);
		}
	}
	return ret;
}

struct sock_calls af_unix_calls = {
	.init = _unix_init_sock,
	.shutdown = _unix_shutdown,
	.bind = _unix_bind,
	.connect = _unix_connect,
	.listen = _unix_listen,
	.accept = _unix_accept,
	.sockpair = _unix_sockpair,
	.send = _unix_send,
	.recv = _unix_recv,
	.select = _unix_select,
	/*
	.sendto = _unix_sendto,
	.recvfrom = _unix_recvfrom,
	*/
};

