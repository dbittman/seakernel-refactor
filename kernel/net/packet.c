#include <net/packet.h>
#include <system.h>
#include <lib/linkedlist.h>
#include <mmu.h>
#include <slab.h>
#include <string.h>
#include <printk.h>
static struct linkedlist buffers;

static void _packet_put(void *o)
{
	struct packet *packet = o;
	if(packet->data) {
		linkedlist_insert(&buffers, (struct linkedentry *)packet->data, packet->data);
	}
}

static void _packet_init(void *o)
{
	struct packet *p = o;
	p->origin = p->sender = NULL;
}

struct kobj kobj_packet = {
	KOBJ_DEFAULT_ELEM(packet),
	.init = _packet_init, .create = NULL,
	.put = _packet_put, .destroy = NULL,
};

__initializer static void _init_buffers(void)
{
	linkedlist_create(&buffers, 0);
}

void *net_packet_buffer_allocate(void)
{
	void *buffer = linkedlist_remove_head(&buffers);
	return buffer == NULL ? (void *)mm_virtual_allocate(0x1000, false) : buffer;
}

