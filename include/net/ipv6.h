#pragma once

#include <slab.h>
#include <net/nic.h>
#include <fs/socket.h>
#define HAS_GLOBAL 1
struct packet;
enum {
	IP_PROTOCOL_ICMP6 = 0x3A,
	IP_PROTOCOL_UDP   = 17,
};

enum {
	PTR_MULTICAST,
	PTR_ANYCAST,
	PTR_UNICAST,
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

struct router_advert_header {
	uint8_t hoplim;
	uint8_t flags;
	uint16_t lifetime;
	uint32_t reachtime;
	uint32_t retranstime;
	uint8_t options[];
} __attribute__((packed));

struct icmp_option {
	uint8_t type;
	uint8_t length;
	uint8_t data[];
} __attribute__((packed));

struct prefix_option_data {
	uint8_t prefixlen;
	uint8_t _res0:6;
	uint8_t autocon:1;
	uint8_t onlink:1;
	uint32_t validtime;
	uint32_t preftime;
	uint32_t _res;
	union ipv6_address prefix;
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

struct prefix {
	struct kobj_header _header;
	struct hashelem elem;
	uint64_t prefix;
	struct nic *nic;
};

struct router {
	struct kobj_header _header;
	struct neighbor *neighbor;
	uint8_t hoplim;
	bool managed_addr;
	bool otherconf;
	uint16_t lifetime;
	bool onlink;
	bool autocon;
	struct prefix *prefix;
};

void ipv6_receive(struct packet *packet, struct ipv6_header *header);
void ipv6_drop_packet(struct packet *packet, struct ipv6_header *header);
void ipv6_reply_packet(struct packet *packet, struct ipv6_header *header, uint16_t *checksum);
uint16_t ipv6_gen_checksum(struct ipv6_header *header);
void ipv6_construct_final(struct packet *packet, struct ipv6_header *header, uint16_t *checksum);
void ipv6_neighbor_update(union ipv6_address lladdr, struct physical_address *paddr, enum reach_state reach);
void ipv6_router_add(struct nic *, struct neighbor *, struct router_advert_header *rah, struct prefix_option_data *pod);
struct neighbor *ipv6_get_neighbor(union ipv6_address *lladdr);

void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type);
void icmp6_neighbor_solicit(struct nic *nic, union ipv6_address lladdr);
void icmp6_router_solicit(struct nic *nic);

int ipv6_network_send(const struct sockaddr *daddr, struct nic *sender, const void *trheader, size_t thlen, const void *msg, size_t mlen, int prot, int);
void ipv6_rawsocket_copy(const struct packet *_packet, struct ipv6_header *header);
