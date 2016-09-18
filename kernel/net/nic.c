#include <net/nic.h>
#include <printk.h>
#include <net/packet.h>
static struct kobj kobj_nic = KOBJ_DEFAULT(nic);
struct kobj kobj_packet = KOBJ_DEFAULT(packet);

extern struct network_protocol network_protocol_ipv6;
struct network_protocol *netprots[NETWORK_TYPE_NUM] = {
	[NETWORK_TYPE_IPV6] = &network_protocol_ipv6,
};

static void _nic_worker(struct worker *worker)
{
	struct nic *nic = worker_arg(worker);
	spinlock_acquire(&nic->lock);
	while(worker_notjoining(worker)) {
		if(nic->rxpending) {
			nic->driver->recv(nic);
		} else {
			struct blockpoint bp;
			blockpoint_create(&bp, 0, 0);
			blockpoint_startblock(&nic->bl, &bp);
			if(!nic->rxpending) {
				spinlock_release(&nic->lock);
				schedule();
				spinlock_acquire(&nic->lock);
			}
			blockpoint_cleanup(&bp);
		}
		/* spinlock_release(&nic->lock); */
		/* schedule(); */
		/* spinlock_acquire(&nic->lock); */
	}
	spinlock_release(&nic->lock);
	worker_exit(worker, 0);
}

void net_nic_send(struct nic *nic, struct packet *packet)
{
	spinlock_acquire(&nic->lock);
	nic->driver->send(nic, packet);
	spinlock_release(&nic->lock);
}

void net_nic_receive(struct nic *nic, void *data, size_t length, int flags)
{
	(void)flags;
	(void)nic;
	(void)data;

	struct packet *packet = kobj_allocate(&kobj_packet);
	packet->data = data;
	packet->length = length;
	packet->origin = nic;
	/* simple link-layer router, for now */
	switch(nic->driver->type) {
		case NIC_TYPE_ETHERNET:
			net_ethernet_receive(packet);
			break;
	}
}

void net_nic_change(struct nic *nic, enum nic_change_event event)
{
	for(int i=0;i<NETWORK_TYPE_NUM;i++) {
		netprots[i]->nic_change(nic, event);
	}
}

struct nic *net_nic_init(void *data, struct nic_driver *drv, void *physaddr, size_t paddrlen)
{
	struct nic *nic = kobj_allocate(&kobj_nic);
	nic->data = data;
	nic->driver = drv;
	if(paddrlen > sizeof(nic->physaddr)) {
		panic(0, "increase size of physical address for nic!");
	}
	memcpy(nic->physaddr.octets, physaddr, paddrlen);
	nic->physaddr.len = paddrlen;
	spinlock_create(&nic->lock);
	nic->rxpending = false;
	blocklist_create(&nic->bl);
	net_nic_change(nic, NIC_CHANGE_CREATE);
	worker_start(&nic->worker, _nic_worker, nic);
	return nic;
}

