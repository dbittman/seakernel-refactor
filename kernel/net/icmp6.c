#include <net/ipv6.h>
#include <net/packet.h>
#include <printk.h>
#include <net/nic.h>

enum {
	ICMP_MSG_ECHO_REQ = 128,
	ICMP_MSG_ECHO_REP = 129,
};

static uint16_t icmp6_gen_checksum(struct ipv6_header *header)
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
	uint32_t prot = HOST_TO_BIG32(58);
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

void icmp6_receive(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)type;
	struct icmp6_header *icmp = (void *)header->data;
	/* printk("Got: %lx:%lx: %x\n", header->source.prefix, header->source.id, icmp->type); */
	/* printk("len = %d, checksum = %x\n", BIG_TO_HOST16(header->length), icmp6_gen_checksum(header)); */
	if(icmp6_gen_checksum(header)) {
		printk("[ipv6]: invalid checksum\n");
	}
	switch(icmp->type) {
		case ICMP_MSG_ECHO_REQ:
			{
				union ipv6_address tmp = header->source;
				//header->source = header->destination;
				

				header->destination = tmp;
				icmp->type = ICMP_MSG_ECHO_REP;
				icmp->checksum = 0;
				icmp->checksum = icmp6_gen_checksum(header);
				packet->sender = packet->origin;
				net_ethernet_send(packet);
			} break;
	}
	(void)packet;
}

