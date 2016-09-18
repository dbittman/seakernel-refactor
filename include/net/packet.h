#pragma once

#include <slab.h>

struct nic;
struct network_address;
struct packet {
	struct kobj_header _header;
	void *data;
	size_t length;
	struct nic *origin, *sender;
	struct network_address *na_source, *na_dest;
	void *netheader;
	struct linkedentry queue_entry;
};

extern struct kobj kobj_packet;
