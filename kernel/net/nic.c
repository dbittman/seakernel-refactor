#include <net/nic.h>
#include <printk.h>
#include <net/packet.h>
static struct kobj kobj_nic = KOBJ_DEFAULT(nic);

static struct kobj kobj_packet = KOBJ_DEFAULT(packet);

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
		spinlock_release(&nic->lock);
		schedule();
		spinlock_acquire(&nic->lock);
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

struct nic *net_nic_init(void *data, struct nic_driver *drv)
{
	struct nic *nic = kobj_allocate(&kobj_nic);
	nic->data = data;
	nic->driver = drv;
	spinlock_create(&nic->lock);
	nic->rxpending = false;
	blocklist_create(&nic->bl);
	worker_start(&nic->worker, _nic_worker, nic);
	return nic;
}

