#pragma once

#include <slab.h>
#include <worker.h>
#include <blocklist.h>

struct nic;
struct packet;

enum {
	NIC_TYPE_ETHERNET,
};
void net_ethernet_receive(struct packet *packet);

struct nic_driver {
	const char *name;
	int (*recv)(struct nic *);
	void (*send)(struct nic *, struct packet *);
	int type;
};

struct nic {
	struct kobj_header _header;
	void *data;
	struct worker worker;
	struct blocklist bl;
	struct spinlock lock;
	_Atomic bool rxpending;

	const struct nic_driver *driver;
};

struct nic *net_nic_init(void *data, struct nic_driver *);
void net_nic_receive(struct nic *nic, void *data, size_t length, int flags);

