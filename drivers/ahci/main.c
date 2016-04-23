#include "ahci.h"
#include <string.h>
#include <mmu.h>
#include <printk.h>
#include <system.h>
#include <drivers/pci.h>
#include <block.h>

static struct kobj kobj_ahci_bus = KOBJ_DEFAULT(ahci_bus);

static struct ahci_bus *interrupt_map[255];

static int _ahci_init_device(struct pci_device *dev)
{
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
	return 0;
}

static struct pci_driver pcidriver = {
	.init_device = _ahci_init_device,
	.name = "ahci",
	.num = 2,

	.supported = {
		{ 0x8086, 0x8c03},
		{ 0x8086, 0x2922},
	},
};

void ahci_interrupt_handler(int int_no, int flags)
{
	(void)flags;
	struct ahci_bus *bus = interrupt_map[int_no - 32];
	for(int i=0;i<32;i++) {
		if(bus->abar->interrupt_status & (1 << i)) {
			bus->abar->ports[i].interrupt_status = ~0;
			bus->abar->interrupt_status = (1 << i);
			ahci_flush_commands((struct hba_port *)&bus->abar->ports[i]);
		}
	}
}

int ahci_port_acquire_slot(struct ahci_device *dev)
{
	while(1) {
		int i;
		/* TODO: blocklist */
		mutex_acquire(&dev->lock);
		for(i=0;i<32;i++)
		{
			if(!(dev->slots & (1 << i))) {
				dev->slots |= (1 << i);
				mutex_release(&dev->lock);
				return i;
			}
		}
		mutex_release(&dev->lock);
	}
}

void ahci_port_release_slot(struct ahci_device *dev, int slot)
{
	mutex_acquire(&dev->lock);
	dev->slots &= ~(1 << slot);
	mutex_release(&dev->lock);
}

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

struct blockdriver ahci_driver = {
	.blksz = ATA_SECTOR_SIZE,
	.read_blocks = _ahci_read,
	.write_blocks = _ahci_write,
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

