#pragma once

union ipv6_address {
	uint8_t octets[16];
	struct {
		uint64_t prefix;
		uint64_t id;
	};
};

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
	uint8_t type;
	uint8_t hoplim;
	
	union ipv6_address source;
	union ipv6_address destination;
} __attribute__((packed));

void ipv6_receive(struct packet *packet, struct ipv6_header *header);

