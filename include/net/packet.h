#pragma once

#include <slab.h>

struct nic;
struct network_address;
struct packet {
	struct kobj_header _header;
	void *data;
	size_t length;
	struct nic *origin, *sender;
	struct linkedentry queue_entry;
};

extern struct kobj kobj_packet;
void *net_packet_buffer_allocate(void);
