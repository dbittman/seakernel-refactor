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

	NETWORK_TYPE_NUM,
};

struct nic_driver {
	const char *name;
	int (*recv)(struct nic *);
	void (*send)(struct nic *, struct packet *);
	int type;
	size_t headlen;
};

enum nic_change_event {
	NIC_CHANGE_CREATE,
	NIC_CHANGE_DELETE,
	NIC_CHANGE_UP,
};

struct network_protocol {
	const char *name;
	void (*nic_change)(struct nic *, enum nic_change_event event);
};

struct physical_address {
	size_t len;
	uint8_t octets[6];
};

struct nic {
	struct kobj_header _header;
	void *data;
	struct worker worker;
	struct blocklist bl;
	struct spinlock lock;
	_Atomic bool rxpending;
	const struct nic_driver *driver;
	struct physical_address physaddr;

	void *netprotdata[NETWORK_TYPE_NUM];
};

struct nic *net_nic_init(void *data, struct nic_driver *, void *, size_t);
void net_nic_receive(struct nic *nic, void *data, size_t length, int flags);
void net_nic_change(struct nic *nic, enum nic_change_event event);

void net_ethernet_receive(struct packet *packet);
void net_ethernet_send(struct packet *packet, int prot, struct physical_address *addr);

