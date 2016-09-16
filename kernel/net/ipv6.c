#include <net/packet.h>
#include <net/nic.h>
#include <net/ipv6.h>
#include <printk.h>

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

