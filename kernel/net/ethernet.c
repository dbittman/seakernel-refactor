#include <net/packet.h>
#include <net/nic.h>
#include <net/ipv6.h>
#include <printk.h>
struct ethernet_frame {
	uint8_t dest[6];
	uint8_t src[6];
	uint16_t type;
	uint8_t data[];
} __attribute__((packed));

enum {
	ETHERTYPE_IPV6 = 0x86DD,
};

void net_ethernet_receive(struct packet *packet)
{
	struct ethernet_frame *ef = packet->data;
	switch(BIG_TO_HOST16(ef->type)) {
		case ETHERTYPE_IPV6:
			printk("Got ipv6 packet!\n");
			ipv6_receive(packet, (struct ipv6_header *)ef->data);
			break;
	}
}

void net_ethernet_send(struct packet *packet)
{
	struct ethernet_frame *ef = packet->data;
	memcpy(ef->dest, ef->src, 6);
	memcpy(ef->src, packet->sender->physaddr, 6);
	packet->sender->driver->send(packet->sender, packet);
}

