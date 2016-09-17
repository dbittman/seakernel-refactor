#include <net/ipv6.h>
#include <net/packet.h>
#include <printk.h>
#include <net/nic.h>

enum {
	ICMP_MSG_ECHO_REQ = 128,
	ICMP_MSG_ECHO_REP = 129,
	ICMP_MSG_NEIGH_SOLICIT = 135,
	ICMP_MSG_NEIGH_ADVERT = 136,
};



void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)type;
	struct icmp6_header *icmp = (void *)header->data;
	struct nicdata *nd = packet->origin->netprotdata[NETWORK_TYPE_IPV6];
	printk("Got: %x\n", icmp->type);
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
				printk("resp echo!\n");
				ipv6_send_packet(packet, header, &icmp->checksum);
			} break;
		case ICMP_MSG_NEIGH_ADVERT:
			{

			} break;
		case ICMP_MSG_NEIGH_SOLICIT:
			{
				struct neighbor_solicit_header *sol = (void *)icmp->data;
				struct neighbor_advert_header *adv = (void *)icmp->data;
				ssize_t options = BIG_TO_HOST16(header->length) - (sizeof(struct icmp6_header) + sizeof(*sol));
				
				if(memcmp(sol->target.octets, nd->linkaddr.octets, 16)
						&& (!(nd->flags & HAS_GLOBAL) || memcmp(sol->target.octets, nd->globaladdr.octets, 16))) {					
					ipv6_drop_packet(packet, header, type);
					break;
				}

				if(options > 0) {
					struct icmp_option *opt = (void *)sol->options;
					if(opt->type == 1) {
						opt->type = 2;
						memcpy(opt->data, packet->origin->physaddr.octets, packet->origin->physaddr.len);
					} else {
						ipv6_drop_packet(packet, header, type);
					}
				}

				header->destination = header->source;
				header->source.addr = 0;
				adv->flags = (1 << 6) | (1 << 5); //TODO: set router flag if we're a router
				icmp->type = ICMP_MSG_NEIGH_ADVERT;
				packet->sender = packet->origin;
				ipv6_send_packet(packet, header, &icmp->checksum);
			} break;
	}
}

