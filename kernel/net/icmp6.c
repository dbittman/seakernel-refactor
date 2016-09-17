#include <net/ipv6.h>
#include <net/packet.h>
#include <printk.h>
#include <net/nic.h>

enum {
	ICMP_MSG_ECHO_REQ = 128,
	ICMP_MSG_ECHO_REP = 129,
	ICMP_MSG_NEIGH_ADVERT = 136,
};



void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)type;
	struct icmp6_header *icmp = (void *)header->data;
	/* printk("Got: %lx:%lx: %x\n", header->source.prefix, header->source.id, icmp->type); */
	/* printk("len = %d, checksum = %x\n", BIG_TO_HOST16(header->length), icmp6_gen_checksum(header)); */
	if(ipv6_gen_checksum(header)) {
		printk("[ipv6]: invalid checksum\n");
	}
	switch(icmp->type) {
		case ICMP_MSG_ECHO_REQ:
			{
				header->destination = header->source;
				header->source.addr = 0;
				icmp->type = ICMP_MSG_ECHO_REP;
				packet->sender = packet->origin;
				ipv6_send_packet(packet, header, &icmp->checksum);
			} break;
		case ICMP_MSG_NEIGH_ADVERT:
			{

			} break;
	}
	(void)packet;
}

