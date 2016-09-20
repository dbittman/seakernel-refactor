#include <net/packet.h>
#include <errno.h>
#include <net/nic.h>
#include <net/ipv6.h>
#include <printk.h>
#include <trace.h>

const struct sockaddr_in6 ipv6_any_address = {
	.sa_family = AF_INET6,
};
TRACE_DEFINE(ipv6_trace, "ipv6");

static struct kobj kobj_router = KOBJ_DEFAULT(router);

static struct router *default_router = NULL;

static struct sleepflag worker_work;

static struct kobj kobj_nicdata = {
	KOBJ_DEFAULT_ELEM(nicdata),
	.destroy = NULL, .put = NULL, .init = NULL, .create = NULL,
};

static void _neighbor_create(void *o)
{
	struct neighbor *n = o;
	spinlock_create(&n->lock);
	linkedlist_create(&n->queue, LINKEDLIST_LOCKLESS);
}

static struct kobj kobj_neighbor = {
	KOBJ_DEFAULT_ELEM(neighbor),
	.create = _neighbor_create,
	.init = NULL, .put = NULL, .destroy = NULL,
};

static struct hash neighbors;
static struct spinlock neighbor_lock;

static struct kobj kobj_prefix = KOBJ_DEFAULT(prefix);

static struct hash prefixes;
static struct spinlock prefix_lock;

void ipv6_neighbor_update(union ipv6_address lladdr, struct physical_address *paddr, enum reach_state reach)
{
	spinlock_acquire(&neighbor_lock);
	struct neighbor *n = hash_lookup(&neighbors, &lladdr, sizeof(lladdr));
	if(!n) {
		n = kobj_allocate(&kobj_neighbor);
		n->addr = lladdr;
		if(paddr) {
			memcpy(&n->physaddr, paddr, sizeof(*paddr));
			printk(":: %ld %x %x %x %x %x %x\n", n->physaddr.len, n->physaddr.octets[0], n->physaddr.octets[1], n->physaddr.octets[2], n->physaddr.octets[3], n->physaddr.octets[4], n->physaddr.octets[5]);
		}
		/* TODO: default to reachable? */
		n->reachability = reach == REACHABILITY_NOCHANGE ? REACHABILITY_REACHABLE : reach;
		hash_insert(&neighbors, &n->addr, sizeof(union ipv6_address), &n->entry, n);
		TRACE(&ipv6_trace, "new neighbor: %lx:%lx -> %x:%x:%x:%x:%x:%x %d", n->addr.prefix, n->addr.id, n->physaddr.octets[0], n->physaddr.octets[1], n->physaddr.octets[2], n->physaddr.octets[3], n->physaddr.octets[4], n->physaddr.octets[5], reach);
	} else {
		spinlock_acquire(&n->lock);
		n->addr = lladdr;
		if(paddr) {
			memcpy(&n->physaddr, paddr, sizeof(*paddr));
		}
		n->reachability = reach == REACHABILITY_NOCHANGE ? n->reachability : reach;
		TRACE(&ipv6_trace, "update neighbor: %lx:%lx -> %lx:%lx: %d", n->addr.prefix, n->addr.id, *(uint32_t *)n->physaddr.octets, *((uint32_t *)n->physaddr.octets + 4), reach);
		spinlock_release(&n->lock);
	}
	sleepflag_wake(&worker_work);
	spinlock_release(&neighbor_lock);
}

struct neighbor *ipv6_get_neighbor(union ipv6_address *lladdr)
{
	spinlock_acquire(&neighbor_lock);
	struct neighbor *n = hash_lookup(&neighbors, lladdr, sizeof(*lladdr));
	if(n) {
		kobj_getref(n);
	}
	spinlock_release(&neighbor_lock);
	return n;
}

void ipv6_router_add(struct nic *nic, struct neighbor *neighbor, struct router_advert_header *rah, struct prefix_option_data *pod)
{
	struct router *router = kobj_allocate(&kobj_router);
	router->hoplim = rah->hoplim;
	router->neighbor = neighbor;
	/* TODO: support flags */
	router->lifetime = BIG_TO_HOST16(rah->lifetime);
	router->onlink = pod->onlink;
	router->autocon = pod->autocon;
	struct router *e = NULL;
	atomic_compare_exchange_strong(&default_router, &e, router);

	spinlock_acquire(&prefix_lock);
	struct prefix *prefix = kobj_allocate(&kobj_prefix);
	prefix->prefix = pod->prefix.prefix;
	prefix->nic = kobj_getref(nic);
	router->prefix = kobj_getref(prefix);
	hash_insert(&prefixes, &prefix->prefix, 8, &prefix->elem, prefix);
	spinlock_release(&prefix_lock);
}

__initializer static void __ipv6_init(void)
{
	spinlock_create(&neighbor_lock);
	hash_create(&neighbors, HASH_LOCKLESS, 1024);
	trace_enable(&ipv6_trace);
	spinlock_create(&prefix_lock);
	hash_create(&prefixes, HASH_LOCKLESS, 32);
	sleepflag_create(&worker_work);
}

static void _ipv6_worker_main(struct worker *w)
{
	printk("[ipv6]: worker thread started\n");
	/* TODO: worker threads have interrupts enabled? */
	while(worker_notjoining(w)) {
		spinlock_acquire(&neighbor_lock);
		struct hashiter iter;
		bool remaining = false;
		for(hash_iter_init(&iter, &neighbors); !hash_iter_done(&iter); hash_iter_next(&iter)) {
			struct neighbor *n = hash_iter_get(&iter);
			spinlock_acquire(&n->lock);
			if(n->reachability == REACHABILITY_REACHABLE && n->queue.count > 0) {
				struct packet *packet = linkedlist_remove_head(&n->queue);
				net_ethernet_send(packet, 0x86DD, &n->physaddr);
				if(n->queue.count > 0) {
					remaining = true;
				}
			}
			spinlock_release(&n->lock);
		}
		spinlock_release(&neighbor_lock);
		if(!remaining) {
			sleepflag_sleep(&worker_work);
		}
	}
	worker_exit(w, 0);
}

static struct worker _ipv6_worker;
static void __ipv6_late_init(void)
{
	worker_start(&_ipv6_worker, _ipv6_worker_main, NULL);
}
LATE_INIT_CALL(__ipv6_late_init, NULL);

static uint8_t ll_prefix[8] = {
	0xfe, 0x80, 0, 0, 0, 0, 0, 0,
};

static void _ipv6_nic_change(struct nic *nic, enum nic_change_event event)
{
	switch(event) {
		struct nicdata *nd;
		case NIC_CHANGE_CREATE:
			nd = nic->netprotdata[NETWORK_TYPE_IPV6] = kobj_allocate(&kobj_nicdata);
			nd->flags = 0;
			nd->nic = nic;
			memcpy(nd->linkaddr.octets, ll_prefix, 8);
			uint8_t id[8];
			memcpy(id, nic->physaddr.octets, 3);
			id[3] = 0xFF;
			id[4] = 0xFE;
			memcpy(id + 5, nic->physaddr.octets + 3, 3);
			id[0] |= 1 << 1;
			memcpy(nd->linkaddr.octets + 8, id, 8);
			for(int i=0;i<8;i++) {
				printk(":: %x\n", id[i]);
			}
			break;
		case NIC_CHANGE_DELETE:
			/* TODO */
			break;
		case NIC_CHANGE_UP:
			/* TODO: notify state thread */
			icmp6_router_solicit(nic);
			break;
		default: panic(0, "invalid nic change event %d\n", event);
	}
}

struct network_protocol network_protocol_ipv6 = {
	.name = "ipv6",
	.nic_change = _ipv6_nic_change,
};

static bool ipv6_neighbor_check_state(struct neighbor *n, struct packet *packet)
{
	bool res = false;
	spinlock_acquire(&n->lock);
	switch(n->reachability) {
		case REACHABILITY_REACHABLE:
			res = true;
			break;
		case REACHABILITY_STALE:
			n->reachability = REACHABILITY_PROBE;
			/* fallthrough */
		case REACHABILITY_INCOMPLETE:
			icmp6_neighbor_solicit(packet->sender, n->addr);
			/* fallthrough */
		default:
			linkedlist_insert(&n->queue, &packet->queue_entry, packet);
	}
	if(!res)
		sleepflag_wake(&worker_work);
	spinlock_release(&n->lock);
	return res;
}

static struct neighbor *ipv6_establish_neighbor(union ipv6_address lladdr, struct packet *packet)
{
	spinlock_acquire(&neighbor_lock);
	struct neighbor *n = hash_lookup(&neighbors, &lladdr, sizeof(lladdr));
	if(n == NULL) {
		n = kobj_allocate(&kobj_neighbor);
		n->addr = lladdr;
		n->reachability = REACHABILITY_INCOMPLETE;
		hash_insert(&neighbors, &n->addr, sizeof(n->addr), &n->entry, n);
	}
	kobj_getref(n);
	spinlock_release(&neighbor_lock);
	if(!ipv6_neighbor_check_state(n, packet)) {
		kobj_putref(n);
		return NULL;
	}
	return n;
}

uint16_t ipv6_gen_checksum(struct ipv6_header *header)
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
	uint32_t prot = HOST_TO_BIG32(header->next_header);
	sum += prot;
	carry = (sum < prot);
	sum += carry;
	
	for(unsigned i=0;i+1<len;i+=2) {
		sum += (*(uint16_t *)(header->data + i));
	}

	if(len & 1) {
		uint16_t tmp = 0;
		memcpy(&tmp, header->data+len-1, 1);
		sum += (tmp);
	}

	while((uint16_t)(sum >> 16))
		sum = (uint16_t)(sum >> 16) + (uint16_t)(sum & 0xFFFF);
	return (~sum);
}

void ipv6_construct_final(struct packet *packet, struct ipv6_header *header, uint16_t *checksum)
{
	struct nicdata *snd = packet->sender->netprotdata[NETWORK_TYPE_IPV6];
	
	/* set source address given a nic */
	header->source = snd->linkaddr;
	header->hoplim = 255;

	/* if we need to update a checksum, do that now */
	if(checksum) {
		*checksum = 0;
		*checksum = ipv6_gen_checksum(header);
	}
}

void ipv6_reply_packet(struct packet *packet, struct ipv6_header *header, uint16_t *checksum)
{
	packet->sender = kobj_getref(packet->origin);
	ipv6_construct_final(packet, header, checksum);
	struct neighbor *n = ipv6_establish_neighbor(header->destination, packet);
	if(n) {
		net_ethernet_send(packet, 0x86DD, &n->physaddr);
		kobj_putref(n);
	}
}

int ipv6_network_send(const struct sockaddr *daddr, struct nic *sender, const void *trheader, size_t thlen, const void *msg, size_t mlen, int prot, int csoff)
{
	struct sockaddr_in6 *dest = (void *)daddr;
	union ipv6_address nexthop = dest->addr;
	if(sender == NULL) {
		spinlock_acquire(&prefix_lock);
		struct prefix *prefix = hash_lookup(&prefixes, &dest->addr.prefix, 8);
		if(!prefix) {
			spinlock_release(&prefix_lock);
			if(default_router == NULL) {
				return -ENETUNREACH;
			}
			sender = kobj_getref(default_router->prefix->nic);
			nexthop = default_router->neighbor->addr;
		} else {
			sender = kobj_getref(prefix->nic);
			spinlock_release(&prefix_lock);
		}
	}

	struct packet *packet = kobj_allocate(&kobj_packet);
	packet->data = net_packet_buffer_allocate();
	packet->sender = sender;

	struct ipv6_header *header = (void *)((char *)packet->data + sender->driver->headlen);
	header->destination = dest->addr;
	header->length = HOST_TO_BIG16(thlen + mlen);
	header->next_header = prot;
	header->version = 6;

	memcpy(header->data, trheader, thlen);
	memcpy(header->data + thlen, msg, mlen);
	
	packet->length = sender->driver->headlen + sizeof(*header) + BIG_TO_HOST16(header->length);

	uint16_t *checksum = NULL;
	if(csoff >= 0) {
		checksum = (uint16_t *)(header->data + csoff);
	}

	ipv6_construct_final(packet, header, checksum);

	if(header->destination.prefix == 0 && BIG_TO_HOST64(header->destination.id) == 1) {
		/* loopback */
		ipv6_receive(packet, header);
		return 0;
	}

	struct neighbor *n = ipv6_establish_neighbor(nexthop, packet);
	if(n) {
		net_ethernet_send(packet, 0x86DD, &n->physaddr);
		kobj_putref(n);
	}
	return 0;
}

struct udp_header;
void udp_recv(struct packet *packet, struct udp_header *header);
void udp_get_ports(struct udp_header *header, uint16_t *src, uint16_t *dest);
static void ipv6_receive_process(struct packet *packet, struct ipv6_header *header, int type)
{
	(void)type;
	packet->transport_header = header->data;
	if(header->next_header == IP_PROTOCOL_ICMP6) {
		icmp6_receive(packet, header, type);
	} else if(header->next_header == IP_PROTOCOL_UDP) {
		struct sockaddr_in6 *saddr = (struct sockaddr_in6 *)&packet->saddr;
		struct sockaddr_in6 *daddr = (struct sockaddr_in6 *)&packet->daddr;
		saddr->flow = 0;
		saddr->scope = 0; //TODO
		daddr->flow = 0;
		daddr->scope = 0;
		saddr->addr = header->source;
		daddr->addr = header->destination;
		saddr->sa_family = AF_INET6;
		daddr->sa_family = AF_INET6;
		udp_get_ports((void *)header->data, &saddr->port, &daddr->port);
		udp_recv(packet, (void *)header->data);
	} else {
		ipv6_drop_packet(packet, header);
	}
}

void ipv6_drop_packet(struct packet *packet, struct ipv6_header *header)
{
	struct nicdata *nd = packet->origin->netprotdata[NETWORK_TYPE_IPV6];
	printk("IPV6 DROP %lx:%lx       %lx:%lx\n", header->destination.prefix, header->destination.id, nd->linkaddr.prefix, nd->linkaddr.id);
	kobj_putref(packet);
}

void ipv6_receive(struct packet *packet, struct ipv6_header *header)
{
	struct nicdata *nd = packet->origin->netprotdata[NETWORK_TYPE_IPV6];
	assert(nd != NULL);

	if(!memcmp(header->destination.octets, nd->linkaddr.octets, 16)
			|| ((nd->flags & HAS_GLOBAL) && !memcmp(header->destination.octets, nd->globaladdr.octets, 16))) {
		ipv6_receive_process(packet, header, PTR_UNICAST);
	} else if(header->destination.octets[0] == 0xFF && header->destination.octets[1] == 0x2) {
		/* TODO: check last octet: 1 = all nodes, 2 = all routers... also can contain part of the ip address, check that too */
		ipv6_receive_process(packet, header, PTR_MULTICAST);
	} else {
		ipv6_drop_packet(packet, header);
	}
}

