#pragma once

#include <slab.h>

struct nic;
struct packet {
	struct kobj_header _header;
	void *data;
	size_t length;
	struct nic *origin, *sender;
};

