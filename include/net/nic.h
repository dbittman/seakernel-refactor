#pragma once

#include <slab.h>
#include <worker.h>
#include <blocklist.h>
#include <lib/linkedlist.h>

struct nic;
struct packet;

enum {
	NIC_TYPE_ETHERNET,
};

enum network_type {
	NETWORK_TYPE_IPV6,
};

struct nic_driver {
	const char *name;
	int (*recv)(struct nic *);
	void (*send)(struct nic *, struct packet *);
	int type;
};

struct network_address {
	uint8_t address[16];
	size_t length;
	enum network_type type;
};

struct nic {
	struct kobj_header _header;
	void *data;
	struct worker worker;
	struct blocklist bl;
	struct spinlock lock;
	_Atomic bool rxpending;

	const struct nic_driver *driver;

	struct linkedlist addresses;
};

struct nic *net_nic_init(void *data, struct nic_driver *);
void net_nic_receive(struct nic *nic, void *data, size_t length, int flags);
bool net_nic_match_netaddr(struct nic *nic, enum network_type type, uint8_t *addr, size_t length);

void net_ethernet_receive(struct packet *packet);

