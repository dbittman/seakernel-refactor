#include <net/packet.h>
#include <net/nic.h>
#include <net/ipv6.h>
#include <printk.h>

#define HAS_GLOBAL 1

struct nicdata {
	struct kobj_header _header;
	int flags;
	union ipv6_address linkaddr;
	union ipv6_address globaladdr;
	struct nic *nic;
};

struct kobj kobj_nicdata = {
	KOBJ_DEFAULT_ELEM(nicdata),
	.destroy = NULL, .put = NULL, .init = NULL, .create = NULL,
};

enum reach_state {
	REACHABILITY_REACHABLE,
	REACHABILITY_INCOMPLETE,
	REACHABILITY_STALE,
	REACHABILITY_PROBE,
	REACHABILITY_DELAY,
};

struct neighbor {
	struct kobj_header _header;
	enum reach_state reachability;
	union ipv6_address addr;
	struct physical_address physaddr;
	struct hashelem entry;
	bool router;
};

struct hash neighbors;

__initializer static void __ipv6_init(void)
{
	hash_create(&neighbors, 0, 1024);
}

static uint8_t ll_prefix[8] = {
	0xfe, 0x80, 0, 0, 0, 0, 0, 0,
};

static void _ipv6_nic_change(struct nic *nic, enum nic_change_event event)
{
	switch(event) {
		struct nicdata *nd;
		case NIC_CHANGE_CREATE:
			nd = nic->netprotdata[NETWORK_TYPE_IPV6] = kobj_allocate(&kobj_nicdata);
			nd->flags = 0;
			nd->nic = nic;
			memcpy(nd->linkaddr.octets, ll_prefix, 8);
			uint8_t id[8];
			memcpy(id, nic->physaddr.octets, 3);
			id[3] = 0xFF;
			id[4] = 0xFE;
			memcpy(id + 5, nic->physaddr.octets, 3);
			id[0] |= 1 << 1;
			memcpy(nd->linkaddr.octets + 8, id, 8);
			break;
		case NIC_CHANGE_DELETE:
			/* TODO */
			break;
		case NIC_CHANGE_UP:
			/* TODO: notify state thread */
			break;
		default: panic(0, "invalid nic change event %d\n", event);
	}
}

struct network_protocol network_protocol_ipv6 = {
	.name = "ipv6",
	.nic_change = _ipv6_nic_change,
};

uint16_t ipv6_gen_checksum(struct ipv6_header *header)
{
	int carry;
	uint32_t sum = 0;
	uint16_t len = BIG_TO_HOST16(header->length);
	for(int i=0;i<4;i++) {
		sum += header->source.u32[i];
		carry = (sum < header->source.u32[i]);
		sum += carry;

		sum += header->destination.u32[i];
		carry = (sum < header->destination.u32[i]);
		sum += carry;
	}

	sum += header->length;
	carry = (sum < header->length);
	sum += carry;
	uint32_t prot = HOST_TO_BIG32(header->next_header);
	sum += prot;
	carry = (sum < prot);
	sum += carry;
	
	for(unsigned i=0;i+1<len;i+=2) {
		sum += (*(uint16_t *)(header->data + i));
	}

	if(len & 1) {
		uint16_t tmp = 0;
		memcpy(&tmp,header->data+len-1,1);
		sum += (tmp);
	}

	while((uint16_t)(sum >> 16))
		sum = (uint16_t)(sum >> 16) + (uint16_t)(sum & 0xFFFF);
	return (~sum);
}

void ipv6_send_packet(struct packet *packet, struct ipv6_header *header, uint16_t *checksum)
{
	/* if no sender nic is set, determine one to send to. */
	if(packet->sender == NULL) {
		printk("we don't route yet\n");
		return;
	}

	struct nicdata *snd = packet->sender->netprotdata[NETWORK_TYPE_IPV6];
	
	/* set source address given a nic */
	header->source = snd->linkaddr;

	/* if we need to update a checksum, do that now */
	if(checksum) {
		*checksum = 0;
		*checksum = ipv6_gen_checksum(header);
	}
	net_ethernet_send(packet);
}

static void ipv6_receive_process(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)packet;
	(void)header;
	(void)type;
	if(header->next_header == IP_PROTOCOL_ICMP6) {
		icmp6_receive(packet, header, type);
	}
}

void ipv6_drop_packet(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)packet;
	(void)header;
	(void)type;
}

void ipv6_receive(struct packet *packet, struct ipv6_header *header)
{
	struct nicdata *nd = packet->origin->netprotdata[NETWORK_TYPE_IPV6];
	assert(nd != NULL);

	if(!memcmp(header->destination.octets, nd->linkaddr.octets, 16)
			|| ((nd->flags & HAS_GLOBAL) && !memcmp(header->destination.octets, nd->globaladdr.octets, 16))) {
		ipv6_receive_process(packet, header, PTR_UNICAST);
	} else if(header->destination.octets[0] == 0xFF && header->destination.octets[1] == 0x2) {
		/* TODO: check last octet: 1 = all nodes, 2 = all routers */
		ipv6_receive_process(packet, header, PTR_MULTICAST);
	}
}

