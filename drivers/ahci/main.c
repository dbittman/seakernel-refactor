#include "ahci.h"
#include <string.h>
#include <mmu.h>
#include <printk.h>
#include <system.h>
#include <drivers/pci.h>
#include <block.h>
#include <interrupt.h>
static struct kobj kobj_ahci_bus = KOBJ_DEFAULT(ahci_bus);

static struct ahci_bus *interrupt_map[255];
static int ahci_port_acquire_slot(struct ahci_device *dev)
{
	/* first try to get a slot without bothering with blocking */
	for(int i=0;i<32;i++) {
		if(!(atomic_fetch_or(&dev->slots, (1 << i)) & (1 << i))) {
			//if(i > 0)
			//if(__j++ % 10 == 0)
				//printk("acquire slot got %d\n", i);
			return i;
		}
	}

	/* okay, that didn't work */
	struct blockpoint bp;
	blockpoint_create(&bp, BLOCK_UNINTERRUPT, 0);

	while(true) {
		blockpoint_startblock(&dev->wait, &bp);
		for(int i=0;i<32;i++) {
			if(!(atomic_fetch_or(&dev->slots, (1 << i)) & (1 << i))) {
				blockpoint_cleanup(&bp);
				return i;
			}
		}
		spinlock_release(&dev->lock);
		schedule();
		blockpoint_cleanup(&bp);
		spinlock_acquire(&dev->lock);
	}
}
#if 0
void ahci_port_release_slot(struct ahci_device *dev, int slot)
{
	mutex_acquire(&dev->lock);
	printk("release port %d\n", slot);
	dev->slots &= ~(1 << slot);
	mutex_release(&dev->lock);
}
#endif

#include <processor.h>
static void ahci_check_eot(struct ahci_bus *bus, int port)
{
	struct ahci_device *dev = &bus->ports[port];
	volatile struct hba_port *hp = &bus->abar->ports[port];

	bool release = false;
	for(int i=0;i<32;i++) {
		struct request *req;
		if((dev->slots & (1ul << i)) && !(hp->command_issue & (1ul << i)) && (req = atomic_exchange(&dev->req_slots[i], NULL))) {
			req->ret_count = req->count;
			blocklist_unblock_all(&req->wait);
			kobj_putref(req);
			dev->slots &= ~(1ul << i);
			hp->sata_error = ~0;
			release = true;
		}
	}
	if(release)
		blocklist_unblock_one(&dev->wait);
}

void ahci_interrupt_handler(int int_no, int flags)
{
	(void)flags;
	struct ahci_bus *bus = interrupt_map[int_no - 32];
	uint32_t intstat = bus->abar->interrupt_status;
	for(int i=0;i<32;i++) {
		if(intstat & (1u << i)) {
			struct ahci_device *dev = &bus->ports[i];
			spinlock_acquire(&dev->lock);
			if(bus->abar->ports[i].interrupt_status & 0x1) {
				ahci_check_eot(interrupt_map[int_no - 32], i);
				/* transfer complete? */
			}
			bus->abar->ports[i].interrupt_status = ~0;
			bus->abar->interrupt_status = (1 << i);
			ahci_flush_commands((struct hba_port *)&bus->abar->ports[i]);
			spinlock_release(&dev->lock);
		}
	}
}

static int _ahci_init_device(struct pci_device *dev)
{
	printk("[ahci]: initializing...\n");
	struct ahci_bus *ab = kobj_allocate(&kobj_ahci_bus);
	ab->pcidev = dev;

	if(!(dev->config.command & 4)) {
		dev->config.command |= 4;
		pci_write_dword(dev->bus, dev->dev, dev->func, 4, dev->config.command);
	}
	ab->abar = (void *)(dev->config.bar5 + PHYS_MAP_START);
	ab->interrupt = dev->config.interrupt_line + 32;
	interrupt_map[ab->interrupt - 32] = ab;
	
	ahci_init_hba(ab);
	ahci_probe_ports(ab);
	interrupt_register(ab->interrupt, ahci_interrupt_handler);
	arch_interrupt_unmask(ab->interrupt);
	return 0;
}

static struct pci_driver pcidriver = {
	.init_device = _ahci_init_device,
	.name = "ahci",
	.num = 2,

	.supported = {
		{ 0x8086, 0x8c03},
		{ 0x8086, 0x2922},
		{ 0x8086, 0x2829},
	},
};

static int handle_req(struct blockdev *bd, struct request *req)
{
	struct ahci_device *dev = bd->devdata;
	uint64_t end_blk = dev->identify.lba48_addressable_sectors;
	if(req->start >= end_blk)
		return 0;
	if((req->start+req->count) > end_blk)
		req->count = end_blk - req->start;
	if(!req->count)
		return 0;
	struct hba_port *port = (struct hba_port *)&dev->bus->abar->ports[dev->idx];
	
	spinlock_acquire(&dev->lock);
	int slot=ahci_port_acquire_slot(dev);
	assert(dev->req_slots[slot] == NULL);
	dev->req_slots[slot] = kobj_getref(req);
	ahci_port_dma_data_transfer(dev->bus->abar, port, dev, slot, 0, req->phys, req->count, req->start);
	spinlock_release(&dev->lock);
	return 1;
}
#if 0
int _ahci_read(struct blockdev *bdev, unsigned long blk, int count, uintptr_t phys)
{
	struct ahci_device *dev = bdev->devdata;
	uint64_t end_blk = dev->identify.lba48_addressable_sectors;
	if(blk >= end_blk)
		return 0;
	if((blk+count) > end_blk)
		count = end_blk - blk;
	if(!count)
		return 0;
	int num_read_blocks = count;
	struct hba_port *port = (struct hba_port *)&dev->bus->abar->ports[dev->idx];
	
	int slot=ahci_port_acquire_slot(dev);
	if(!ahci_port_dma_data_transfer(dev->bus->abar, port, dev, slot, 0, phys, count, blk))
		num_read_blocks = 0;
	
	ahci_port_release_slot(dev, slot);
	
	return num_read_blocks * ATA_SECTOR_SIZE;
}

int _ahci_write(struct blockdev *bdev, unsigned long blk, int count, uintptr_t phys)
{
	struct ahci_device *dev = bdev->devdata;
	uint64_t end_blk = dev->identify.lba48_addressable_sectors;
	if(blk >= end_blk)
		return 0;
	if((blk+count) > end_blk)
		count = end_blk - blk;
	if(!count)
		return 0;
	int num_read_blocks = count;
	struct hba_port *port = (struct hba_port *)&dev->bus->abar->ports[dev->idx];
	
	int slot=ahci_port_acquire_slot(dev);
	if(!ahci_port_dma_data_transfer(dev->bus->abar, port, dev, slot, 1, phys, count, blk))
		num_read_blocks = 0;
	
	ahci_port_release_slot(dev, slot);
	
	return num_read_blocks * ATA_SECTOR_SIZE;
}
#endif
struct blockdriver ahci_driver = {
	.blksz = ATA_SECTOR_SIZE,
	.read_blocks = 0,
	.write_blocks = 0,
	.handle_req = handle_req,
	.name = "ahci",
	.kobj_block = {
		.initialized = false,
		.name = "kobj_block_ahci",
		.size = sizeof(struct block) + 512,
		.put = NULL, .init = NULL, .create = NULL, .destroy = NULL,
	},
};

__orderedinitializer(__orderedafter(PCI_INITIALIZER_ORDER + DEVICE_INITIALIZER_ORDER))
static void _init_ahci(void)
{
	pci_register_driver(&pcidriver);
	blockdriver_register(&ahci_driver);
}

