#pragma once

#include <slab.h>
#include <fs/socket.h>

struct nic;
struct network_address;
struct packet {
	struct kobj_header _header;
	void *data;
	size_t length;
	struct nic *origin, *sender;
	struct linkedentry queue_entry;
	void *transport_header;
	void *network_header;
	struct sockaddr saddr;
	struct sockaddr daddr;
};

extern struct kobj kobj_packet;
void *net_packet_buffer_allocate(void);
struct packet *packet_duplicate(const struct packet *src);

