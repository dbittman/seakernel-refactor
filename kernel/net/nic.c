#include <net/nic.h>
#include <printk.h>
#include <net/packet.h>

static void _nic_put(void *o)
{
	(void)o;
	panic(0, "no!\n");
}

static struct kobj kobj_nic = {
	KOBJ_DEFAULT_ELEM(nic),
	.put = _nic_put, .init = NULL, .create = NULL, .destroy = NULL,
};

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
	packet->origin = kobj_getref(nic);
	/* simple link-layer router, for now */
	switch(nic->driver->type) {
		case NIC_TYPE_ETHERNET:
			net_ethernet_receive(packet);
			break;
		default:
			kobj_putref(packet);
	}
}

void net_nic_change(struct nic *nic, enum nic_change_event event)
{
	for(int i=0;i<NETWORK_TYPE_NUM;i++) {
		netprots[i]->nic_change(nic, event);
	}
}

static struct hash nic_names;
static struct hash nic_nums;
static _Atomic int next_nic = 0;
__initializer static void _init_nic_names(void)
{
	hash_create(&nic_names, 0, 32);
	hash_create(&nic_nums, 0, 32);
}

struct nic *net_nic_get_byname(const char *name)
{
	/* TODO: this is not thread safe */
	struct nic *n = hash_lookup(&nic_names, name, strlen(name));
	return n ? kobj_getref(n) : NULL;
}

struct nic *net_nic_get_bynum(int num)
{
	/* TODO: this is not thread safe */
	struct nic *n = hash_lookup(&nic_nums, &num, sizeof(num));
	return n ? kobj_getref(n) : NULL;
}

static int _next_id = 0;
struct nic *net_nic_init(void *data, struct nic_driver *drv, void *physaddr, size_t paddrlen)
{
	struct nic *nic = kobj_allocate(&kobj_nic);
	nic->data = data;
	nic->driver = drv;
	if(paddrlen > sizeof(nic->physaddr)) {
		panic(0, "increase size of physical address for nic!");
	}
	snprintf(nic->name, 16, "nic%d", next_nic++);
	nic->id = ++_next_id;
	hash_insert(&nic_names, nic->name, strlen(nic->name), &nic->elem, kobj_getref(nic));
	hash_insert(&nic_nums, &nic->id, sizeof(int), &nic->elemnum, kobj_getref(nic));
	memcpy(nic->physaddr.octets, physaddr, paddrlen);
	nic->physaddr.len = paddrlen;
	spinlock_create(&nic->lock);
	nic->rxpending = false;
	blocklist_create(&nic->bl);
	net_nic_change(nic, NIC_CHANGE_CREATE);
	worker_start(&nic->worker, _nic_worker, nic);
	return nic;
}

