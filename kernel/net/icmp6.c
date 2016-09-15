#include <net/ipv6.h>
#include <net/packet.h>
#include <printk.h>

void icmp6_receive(struct packet *packet, struct ipv6_header *header)
{
	struct icmp6_header *icmp = (void *)header->data;
	printk("Got: %x\n", icmp->type);
	(void)packet;
}

