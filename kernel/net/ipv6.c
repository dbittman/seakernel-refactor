#include <net/packet.h>
#include <net/nic.h>
#include <net/ipv6.h>
#include <printk.h>

void ipv6_receive(struct packet *packet, struct ipv6_header *header)
{
	(void)packet;
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
}

