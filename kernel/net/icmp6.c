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

void icmp6_neighbor_solicit(struct nic *nic, union ipv6_address lladdr)
{
	/* TODO: simplify this, or encapsulate */
	struct packet *packet = kobj_allocate(&kobj_packet);
	packet->sender = nic;
	packet->data = net_packet_buffer_allocate();

	struct ipv6_header *header = (void *)((char *)packet->data + nic->driver->headlen);
	memset(&header->destination, 0, sizeof(union ipv6_address));
	/* solicited node multicast */
	header->destination.octets[0] = 0xFF;
	header->destination.octets[1] = 0x02;
	header->destination.octets[11] = 0x01;
	header->destination.octets[12] = 0xFF;
	header->destination.octets[13] = lladdr.octets[13];
	header->destination.octets[14] = lladdr.octets[14];
	header->destination.octets[15] = lladdr.octets[15];
	header->length = HOST_TO_BIG16(32);
	header->next_header = 58;
	header->version = 6;
	struct icmp6_header *icmp = (void *)header->data;
	icmp->type = ICMP_MSG_NEIGH_SOLICIT;
	icmp->code = 0;
	struct neighbor_solicit_header *sol = (void *)icmp->data;
	sol->target = lladdr;
	struct icmp_option *opt = (void *)sol->options;
	opt->type = 1;
	opt->length = 1; //TODO: don't hardcode
	packet->length = nic->driver->headlen + sizeof(struct ipv6_header) + BIG_TO_HOST16(header->length);

	/* TODO: make not ethernet specific */
	memcpy(opt->data, nic->physaddr.octets, 6);
	printk("Sending solicitation\n");
	struct physical_address pa;
	memset(&pa, 0, sizeof(pa));
	pa.octets[0] = 0x33;
	pa.octets[1] = 0x33;
	pa.octets[2] = lladdr.octets[12];
	pa.octets[3] = lladdr.octets[13];
	pa.octets[4] = lladdr.octets[14];
	pa.octets[5] = lladdr.octets[15];
	ipv6_construct_final(packet, header, &icmp->checksum);
	net_ethernet_send(packet, 0x86DD, &pa);
}

void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)type;
	struct icmp6_header *icmp = (void *)header->data;
	struct nicdata *nd = packet->origin->netprotdata[NETWORK_TYPE_IPV6];
	/* printk("Got: %x\n", icmp->type); */
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
				struct neighbor_advert_header *adv = (void *)icmp->data;
				ssize_t options = BIG_TO_HOST16(header->length) - (sizeof(struct icmp6_header) + sizeof(*adv));
				if(options == 0) {
					/* we must be provided the link-layer address */
					ipv6_drop_packet(packet, header);
					break;
				}
				
				struct icmp_option *opt = (void *)adv->options;
				
				while(options > 0) {
					if(opt->type == 2) {
						struct physical_address pa;
						pa.len = opt->length * 8 - 2;
						memcpy(pa.octets, opt->data, pa.len);
						ipv6_neighbor_update(adv->target, &pa, REACHABILITY_REACHABLE);
						break;
					}
					options -= opt->length * 8;
					opt = (void *)((char *)opt + opt->length * 8);
				}
				kobj_putref(packet);
			} break;
		case ICMP_MSG_NEIGH_SOLICIT:
			{
				struct neighbor_solicit_header *sol = (void *)icmp->data;
				struct neighbor_advert_header *adv = (void *)icmp->data;
				ssize_t options = BIG_TO_HOST16(header->length) - (sizeof(struct icmp6_header) + sizeof(*sol));
				
				if(memcmp(sol->target.octets, nd->linkaddr.octets, 16)
						&& (!(nd->flags & HAS_GLOBAL) || memcmp(sol->target.octets, nd->globaladdr.octets, 16))) {					
					ipv6_drop_packet(packet, header);
					break;
				}
				
				if(options == 0) {
					int o = packet->origin->physaddr.len + 2;
					o = ((options - 1) & ~7) + 8;
					header->length += o;
					packet->length += o;
				}
				struct icmp_option *opt = (void *)sol->options;
				if(options != 0) {
					if(opt->type == 1) {
						struct physical_address pa;
						pa.len = opt->length * 8 - 2;
						memcpy(pa.octets, opt->data, pa.len);
						ipv6_neighbor_update(header->source, &pa, REACHABILITY_REACHABLE);
					}
				}
				opt->type = 2;
				memcpy(opt->data, packet->origin->physaddr.octets, packet->origin->physaddr.len);

				header->destination = header->source;
				header->source.addr = 0;
				adv->flags = (1 << 6) | (1 << 5); //TODO: set router flag if we're a router
				icmp->type = ICMP_MSG_NEIGH_ADVERT;
				packet->sender = packet->origin;
				ipv6_send_packet(packet, header, &icmp->checksum);
			} break;
		default:
			ipv6_drop_packet(packet, header);
	}
}

