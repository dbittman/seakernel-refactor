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

void net_ethernet_drop(struct packet *packet)
{
	(void)packet;
}

void net_ethernet_receive(struct packet *packet)
{
	struct ethernet_frame *ef = packet->data;
	if((ef->dest[0] & 1) //multicast
			|| (!memcmp(ef->dest, packet->origin->physaddr.octets, 6))) {
		switch(BIG_TO_HOST16(ef->type)) {
			case ETHERTYPE_IPV6:
				printk("Got ipv6 packet!\n");
				ipv6_receive(packet, (struct ipv6_header *)ef->data);
				break;
		}
	} else {
		net_ethernet_drop(packet);
	}
}

void net_ethernet_send(struct packet *packet, int prot, struct physical_address *addr)
{
	struct ethernet_frame *ef = packet->data;
	memcpy(ef->dest, addr->octets, 6);
	memcpy(ef->src, packet->sender->physaddr.octets, 6);
	ef->type = HOST_TO_BIG16(prot);
	packet->sender->driver->send(packet->sender, packet);
}

