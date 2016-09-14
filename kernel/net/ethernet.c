#include <net/packet.h>
#include <net/nic.h>
#include <printk.h>
struct ethernet_frame {
	uint8_t dest[6];
	uint8_t src[6];
	uint16_t type;
};

void net_ethernet_receive(struct packet *packet)
{
	struct ethernet_frame *ef = packet->data;
	printk("%x\n", ef->type);
}

