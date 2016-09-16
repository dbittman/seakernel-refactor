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
};

struct kobj kobj_nicdata = KOBJ_DEFAULT(nicdata);

static void _ipv6_nic_change(struct nic *nic, enum nic_change_event event)
{
	switch(event) {
		case NIC_CHANGE_CREATE:
			nic->netprotdata[NETWORK_TYPE_IPV6] = kobj_allocate(&kobj_nicdata);
			break;
		case NIC_CHANGE_DELETE:
			/* TODO */
			break;
		default: panic(0, "invalid nic change event %d\n", event);
	}
}

struct network_protocol network_protocol_ipv6 = {
	.name = "ipv6",
	.nic_change = _ipv6_nic_change,
};

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
	(void)packet;
#if 0
	printk("IPV6: %d\n", header->version);

	printk("length = %d\n", BIG_TO_HOST16(header->length));
	for(int i=0;i<16;i++) {
		printk("%x ", header->source.octets[i]);
	}
	printk("\n");
	for(int i=0;i<16;i++) {
		printk("%x ", header->destination.octets[i]);
	}
	printk("\n");
#endif
	//if(net_nic_match_netaddr(packet->origin, NETWORK_TYPE_IPV6, header->destination.octets, 16)) {
		ipv6_receive_process(packet, header, PTR_UNICAST);
	//} else if(header->destination.octets[0] == 0xFF && header->destination.octets[1] == 0x2) {
	//	ipv6_receive_process(packet, header, PTR_MULTICAST);
	//}
}

