#include "ahci.h"
#include <string.h>
#include <thread.h>
#include <printk.h>

struct hba_command_header *ahci_initialize_command_header(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev, int slot, int write, int atapi, int prd_entries, int fis_len)
{
	(void)abar;
	(void)port;
	struct hba_command_header *h = (struct hba_command_header *)dev->clb_virt;
	h += slot;
	h->write=write ? 1 : 0;
	h->prdb_count=0;
	h->atapi=atapi ? 1 : 0;
	h->fis_length = fis_len;
	h->prdt_len=prd_entries;
	h->prefetchable=0;
	h->bist=0;
	h->pmport=0;
	h->reset=0;
	return h;
}

struct fis_reg_host_to_device *ahci_initialize_fis_host_to_device(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev, int slot, int cmdctl, int ata_command)
{
	(void)abar;
	(void)port;
	struct hba_command_table *tbl = (struct hba_command_table *)(dev->ch[slot]);
	struct fis_reg_host_to_device *fis = (struct fis_reg_host_to_device *)(tbl->command_fis);
	
	memset(fis, 0, sizeof(*fis));
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->command = ata_command;
	fis->c=cmdctl?1:0;
	return fis;
}

void ahci_send_command(struct hba_port *port, int slot)
{
	port->command_issue = (1 << slot);
	ahci_flush_commands(port);
	port->interrupt_enable = AHCI_DEFAULT_INT;
}

int ahci_write_prdt(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev, int slot, int offset, int length, uintptr_t phys_buffer)
{
	(void)abar;
	(void)port;
	int num_entries = ((length-1) / PRDT_MAX_COUNT) + 1;
	struct hba_command_table *tbl = (struct hba_command_table *)(dev->ch[slot]);
	int i;
	struct hba_prdt_entry *prd;
	for(i=0;i<num_entries-1;i++)
	{
		/* TODO: do we need to do this? */
		prd = &tbl->prdt_entries[i+offset];
		prd->byte_count = PRDT_MAX_COUNT-1;
		prd->data_base_l = LOWER32(phys_buffer);
		prd->data_base_h = UPPER32(phys_buffer);
		prd->interrupt_on_complete=0;
		
		length -= PRDT_MAX_COUNT;
		phys_buffer += PRDT_MAX_COUNT;
	}
	prd = &tbl->prdt_entries[i+offset];
	prd->byte_count = length-1;
	prd->data_base_l = LOWER32(phys_buffer);
	prd->data_base_h = UPPER32(phys_buffer);
	prd->interrupt_on_complete=0;
	
	return num_entries;
}

int ahci_port_dma_data_transfer(struct hba_memory *abar, struct hba_port *port, struct ahci_device *dev, int slot, int write, uintptr_t virt_buffer, int sectors, uint64_t lba)
{
	int timeout;
	int fis_len = sizeof(struct fis_reg_host_to_device) / 4;
	int ne = ahci_write_prdt(abar, port, dev,
			slot, 0, ATA_SECTOR_SIZE * sectors, virt_buffer);
	struct hba_command_header *h = ahci_initialize_command_header(abar,
			port, dev, slot, write, 0, ne, fis_len);
	(void)h;
	struct fis_reg_host_to_device *fis = ahci_initialize_fis_host_to_device(abar,
			port, dev, slot, 1, write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX);
	fis->device = 1<<6;
	/* WARNING: assumes little-endian */
	fis->count_l = sectors & 0xFF;
	fis->count_h = (sectors >> 8) & 0xFF;
	
	fis->lba0 = (unsigned char)( lba        & 0xFF);
	fis->lba1 = (unsigned char)((lba >> 8)  & 0xFF);
	fis->lba2 = (unsigned char)((lba >> 16) & 0xFF);
	fis->lba3 = (unsigned char)((lba >> 24) & 0xFF);
	fis->lba4 = (unsigned char)((lba >> 32) & 0xFF);
	fis->lba5 = (unsigned char)((lba >> 40) & 0xFF);
	port->sata_error = ~0;
	timeout = ATA_TFD_TIMEOUT;
	while ((port->task_file_data & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && --timeout)
	{
		asm("pause");
		//schedule();
	}
	if(!timeout) goto port_hung;
	
	port->sata_error = ~0;
	ahci_send_command(port, slot);

	return 1;

	timeout = ATA_TFD_TIMEOUT;
	while ((port->task_file_data & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && --timeout)
	{
		schedule();
	}
	if(!timeout) goto port_hung;
	
	timeout = AHCI_CMD_TIMEOUT;
	while(--timeout)
	{
		if(!((port->sata_active | port->command_issue) & (1 << slot)))
			break;
		schedule();
	}
	if(!timeout) goto port_hung;
	if(port->sata_error)
	{
		printk("[ahci]: device %d: ahci error\n", dev->idx);
		goto error;
	}
	if(port->task_file_data & ATA_DEV_ERR)
	{
		printk("[ahci]: device %d: task file data error\n", dev->idx);
		goto error;
	}
	return 1;
	port_hung:
	printk("[ahci]: device %d: port hung\n", dev->idx);
	error:
	printk("[ahci]: device %d: tfd=%x, serr=%x\n",
			dev->idx, port->task_file_data, port->sata_error);
	ahci_reset_device(abar, port, dev);
	return 0;
}

int ahci_device_identify_ahci(struct hba_memory *abar,
		struct hba_port *port, struct ahci_device *dev)
{
	int fis_len = sizeof(struct fis_reg_host_to_device) / 4;
	uintptr_t dma;
	dma = mm_physical_allocate(0x1000, false);
	ahci_write_prdt(abar, port, dev, 0, 0, 512, dma);
	struct hba_command_header *h = ahci_initialize_command_header(abar,
			port, dev, 0, 0, 0, 1, fis_len);
	struct fis_reg_host_to_device *fis = ahci_initialize_fis_host_to_device(abar,
			port, dev, 0, 1, ATA_CMD_IDENTIFY);
	(void)h;
	(void)fis;
	int timeout = ATA_TFD_TIMEOUT;
	port->sata_error = ~0;
	while ((port->task_file_data & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && --timeout)
		cpu_pause();
	if(!timeout)
	{
		printk("[ahci]: device %d: identify 1: port hung\n", dev->idx);
		printk("[ahci]: device %d: identify 1: tfd=%x, serr=%x\n",
				dev->idx, port->task_file_data, port->sata_error);
		mm_physical_deallocate(dma);
		return 0;
	}
	ahci_send_command(port, 0);
	timeout = AHCI_CMD_TIMEOUT;
	while(--timeout)
	{
		if(!((port->sata_active | port->command_issue) & 1))
			break;
		cpu_pause();
	}
	if(!timeout)
	{
		printk("[ahci]: device %d: identify 2: port hung\n", dev->idx);
		printk("[ahci]: device %d: identify 2: tfd=%x, serr=%x\n",
				dev->idx, port->task_file_data, port->sata_error);
		mm_physical_deallocate(dma);
		return 0;
	}
	
	memcpy(&dev->identify, (void *)(dma + PHYS_MAP_START), sizeof(struct ata_identify));
	mm_physical_deallocate(dma);
	printk("[ahci]: device %d: num sectors=%ld\n", dev->idx,
			dev->identify.lba48_addressable_sectors);
	return 1;
}

