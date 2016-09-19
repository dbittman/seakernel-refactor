#include <net/network.h>
#include <fs/socket.h>
#include <net/ipv6.h>
struct nic;
static void (*senders[MAX_AF + 1])(const struct sockaddr *, struct nic *, const void *, size_t, const void *, size_t, int, int) = {
	[AF_INET6] = ipv6_network_send,
};

static int protocol_map[MAX_AF + 1][MAX_PROT] = {
	[AF_INET6] = { [PROT_UDP] = 17 },
};

void net_network_send(struct socket *sock, const struct sockaddr *dest, const void *trheader, size_t thlen, const void *msg, size_t mlen, int prot, int checksum_offset)
{
	if(senders[dest->sa_family])
		senders[dest->sa_family](dest, sock->nic, trheader, thlen, msg, mlen, protocol_map[dest->sa_family][prot], checksum_offset);
}

