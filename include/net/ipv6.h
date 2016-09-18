#pragma once

#include <slab.h>
#include <net/nic.h>
#define HAS_GLOBAL 1
struct packet;
enum {
	IP_PROTOCOL_ICMP6 = 0x3A,
};

enum {
	PTR_MULTICAST,
	PTR_ANYCAST,
	PTR_UNICAST,
};

union ipv6_address {
	uint8_t octets[16];
	uint32_t u32[4];
	struct {
		uint64_t prefix;
		uint64_t id;
	} __attribute__((packed));
	__int128 addr;
};

struct icmp6_header {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint8_t data[];
} __attribute__((packed));

struct neighbor_solicit_header {
	uint32_t _res;
	union ipv6_address target;
	uint8_t options[];
} __attribute__((packed));

struct neighbor_advert_header {
	uint8_t flags;
	uint8_t _resv[3];
	union ipv6_address target;
	uint8_t options[];
} __attribute__((packed));

struct icmp_option {
	uint8_t type;
	uint8_t length;
	uint8_t data[];
} __attribute__((packed));

struct ipv6_header {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	uint32_t tc0 : 4;
	uint32_t version : 4;
	uint32_t tcfl : 24;
#else
	uint32_t version : 4;
	uint32_t tcfl : 28;
#endif
	uint16_t length;
	uint8_t next_header;
	uint8_t hoplim;
	
	union ipv6_address source;
	union ipv6_address destination;
	uint8_t data[];
} __attribute__((packed));

struct nicdata {
	struct kobj_header _header;
	int flags;
	union ipv6_address linkaddr;
	union ipv6_address globaladdr;
	struct nic *nic;
};

enum reach_state {
	REACHABILITY_REACHABLE,
	REACHABILITY_INCOMPLETE,
	REACHABILITY_STALE,
	REACHABILITY_PROBE,
	REACHABILITY_DELAY,
	REACHABILITY_NOCHANGE,
};

struct neighbor {
	struct kobj_header _header;
	union ipv6_address addr;
	enum reach_state reachability;
	struct physical_address physaddr;
	struct hashelem entry;
	struct spinlock lock;
	struct linkedlist queue;
	bool router;
};

struct router {
	struct kobj_header _header;
	struct neighbor *neighbor;
};

void ipv6_receive(struct packet *packet, struct ipv6_header *header);
void ipv6_drop_packet(struct packet *packet, struct ipv6_header *header, int type);
void ipv6_send_packet(struct packet *packet, struct ipv6_header *header, uint16_t *);
uint16_t ipv6_gen_checksum(struct ipv6_header *header);
void ipv6_construct_final(struct packet *packet, struct ipv6_header *header, uint16_t *checksum);
void ipv6_neighbor_update(union ipv6_address lladdr, struct physical_address *paddr, enum reach_state reach);

void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type);
void icmp6_neighbor_solicit(struct nic *nic, union ipv6_address lladdr);

