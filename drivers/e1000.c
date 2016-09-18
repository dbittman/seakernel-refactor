#include <drivers/pci.h>
#include <device.h>
#include "e1000.h"
#include <mmu.h>
#include <printk.h>
#include <panic.h>
#include <interrupt.h>
#include <net/packet.h>

struct linkedlist list;

struct kobj kobj_e1000_device = KOBJ_DEFAULT(e1000_device);

static void writecmd(struct e1000_device *e, uint16_t reg, uint32_t val)
{
	if(e->bar_type == 0) {
		*(volatile uint32_t *)(e->mem_base + reg) = val;
	} else {
		panic(0, "NI");
	}
}

static uint32_t readcmd(struct e1000_device *e, uint16_t reg)
{
	if(e->bar_type == 0) {
		return *(volatile uint32_t *)(e->mem_base + reg);
	} else {
		panic(0, "NI");
	}
}

static void e1000_rxinit(struct e1000_device *e)
{
	e->rx_descs = (void *)mm_virtual_allocate(0x1000, false);
	for(int i=0;i<E1000_NUM_RX_DESC;i++) {
		e->rx_descs[i].addr = mm_physical_allocate(0x1000, false);
		e->rx_descs[i].status = 0;
	}

	uintptr_t ptr = (uintptr_t)e->rx_descs - PHYS_MAP_START;
	writecmd(e, REG_RXDESCLO, (uint32_t)(ptr & 0xFFFFFFFF));
	writecmd(e, REG_RXDESCHI, (uint32_t)(ptr >> 32));

	writecmd(e, REG_RXDESCLEN, E1000_NUM_RX_DESC * 16);
	writecmd(e, REG_RXDESCHEAD, 0);
	writecmd(e, REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);
	e->rx_cur = 0;
	writecmd(e, REG_RCTRL, RCTL_EN | RCTL_SBP | RCTL_UPE | RCTL_MPE | RCTL_LBM_NONE | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048);
}

static void e1000_txinit(struct e1000_device *e)
{
	e->tx_descs = (void *)mm_virtual_allocate(0x1000, false);
	for(int i=0;i<E1000_NUM_TX_DESC;i++) {
		e->tx_descs[i].addr = e->tx_descs[i].cmd = 0;
		e->tx_descs[i].status = TSTA_DD;
	}

	uintptr_t ptr = (uintptr_t)e->tx_descs - PHYS_MAP_START;
	writecmd(e, REG_TXDESCLO, (uint32_t)(ptr & 0xFFFFFFFF));
	writecmd(e, REG_TXDESCHI, (uint32_t)(ptr >> 32));

	writecmd(e, REG_TXDESCLEN, E1000_NUM_TX_DESC * 16);
	writecmd(e, REG_TXDESCHEAD, 0);
	writecmd(e, REG_TXDESCTAIL, 0);
	e->tx_cur = 0;
	writecmd(e, REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) | (64 << TCTL_COLD_SHIFT) | TCTL_RTLC);
	//writecmd(e, REG_TIPG, 0x0060200A); //???
}

static bool e1000_haseeprom(struct e1000_device *e)
{
	writecmd(e, REG_EEPROM, 1);
	for(int i=0;i<1000 && !e->eeprom_pres;i++) {
		if(readcmd(e, REG_EEPROM) & 0x10) {
			e->eeprom_pres = true;
		}
	}
	return e->eeprom_pres;
}

static uint32_t eepromread(struct e1000_device *e, uint8_t addr)
{
	uint32_t tmp;
	if(e->eeprom_pres) {
		writecmd(e, REG_EEPROM, 1 | ((uint32_t)addr << 8));
		while(!((tmp = readcmd(e, REG_EEPROM)) & (1 << 4)));
	} else {
		writecmd(e, REG_EEPROM, 1 | ((uint32_t)addr << 2));
		while(!((tmp = readcmd(e, REG_EEPROM)) & (1 << 1)));
	}
	return (uint16_t)((tmp >> 16) & 0xFFFF);
}

static bool e1000_initmac(struct e1000_device *e)
{
	if(e->eeprom_pres) {
		uint32_t tmp;
		tmp = eepromread(e, 0);
		e->mac[0] = tmp & 0xFF;
		e->mac[1] = tmp >> 8;
		tmp = eepromread(e, 1);
		e->mac[2] = tmp & 0xFF;
		e->mac[3] = tmp >> 8;
		tmp = eepromread(e, 2);
		e->mac[4] = tmp & 0xFF;
		e->mac[5] = tmp >> 8;
	} else {
		uint8_t *mem_base_mac_8 = (uint8_t *)(e->mem_base + 0x5400);
		uint32_t *mem_base_mac_32 = (uint32_t *)(e->mem_base + 0x5400);
		if(mem_base_mac_32[0] != 0) {
			for(int i=0;i<6;i++) {
				e->mac[i] = mem_base_mac_8[i];
			}
		} else {
			return false;
		}
	}
	return true;
}

static void e1000_startlink(struct e1000_device *e)
{
	uint32_t tmp = readcmd(e, REG_CTRL);
	writecmd(e, REG_CTRL, tmp | ECTRL_SLU);
}

static struct e1000_device *int_map[256];

static void e1000_interrupt_handler(int int_no, int flags)
{
	(void)flags;
	struct e1000_device *e = int_map[int_no];
	if(e == NULL) {
		return;
	}
	spinlock_acquire(&e->nic->lock);
	uint32_t status = readcmd(e, 0xc0);
	if(status & 0x04) {
		e1000_startlink(e);
	}

	if(status & 0x80) {
		e->nic->rxpending = true;
		blocklist_unblock_one(&e->nic->bl);
	}
	spinlock_release(&e->nic->lock);
}

void serial_putc (char c);
static int _e1000_recv(struct nic *nic)
{
	int count=0;
	struct e1000_device *e = nic->data;
	while(e->rx_descs[e->rx_cur].status & 0x1) {
		uintptr_t pdata = e->rx_descs[e->rx_cur].addr;
		size_t len = e->rx_descs[e->rx_cur].length;

		e->rx_descs[e->rx_cur].addr = mm_physical_allocate(0x1000, false);
		net_nic_receive(nic, (void *)(pdata + PHYS_MAP_START), len, 0 /* TODO flags */);

		e->rx_descs[e->rx_cur].status = 0;
		uint16_t old = e->rx_cur;
		e->rx_cur = (e->rx_cur + 1) % E1000_NUM_RX_DESC;
		writecmd(e, REG_RXDESCTAIL, old);
		count++;
	}
	if(count == 0)
		nic->rxpending = false;
	return count;
}

static void _e1000_send(struct nic *nic, struct packet *packet)
{
	struct e1000_device *e = nic->data;
	struct e1000_tx_desc *d = &e->tx_descs[e->tx_cur];
	d->addr = (uintptr_t)packet->data - PHYS_MAP_START;
	d->length = packet->length;
	d->cmd = CMD_EOP | CMD_IFCS | CMD_RS | CMD_RPS;
	d->status = 0;
	uint16_t old = e->tx_cur;
	e->tx_cur = (old + 1) % E1000_NUM_TX_DESC;
	writecmd(e, REG_TXDESCTAIL, e->tx_cur);
	while(!(d->status & 0xFF)); //TODO: handle packet sending in background via interrupts
	kobj_putref(packet);
}

static struct nic_driver e1000_nic_driver = {
	.name = "e1000",
	.headlen = 14,
	.type = NIC_TYPE_ETHERNET,
	.send = _e1000_send,
	.recv = _e1000_recv,
};

static int _e1000_init_device(struct pci_device *dev)
{
	struct e1000_device *e = kobj_allocate(&kobj_e1000_device);
	e->pci = dev;
	linkedlist_insert(&list, &e->entry, e);

	printk("[e1000]: init device\n");
	e->bar_type = dev->config.bar[0] & 1;
	e->io_base = pci_get_bar(dev, PCI_BAR_IO);
	e->mem_base = pci_get_bar(dev, PCI_BAR_MEM) + PHYS_MAP_START;

	e->intno = dev->config.interrupt_line + 32;
	interrupt_register(e->intno, e1000_interrupt_handler);
	arch_interrupt_unmask(e->intno);
	assert(int_map[e->intno] == NULL);
	int_map[e->intno] = e;

	if(!(dev->config.command & 4)) {
		dev->config.command |= 4;
		pci_write_dword(dev->bus, dev->dev, dev->func, 4, dev->config.command);
	}
	if(!e1000_haseeprom(e)) {
		printk("[e1000]: warning - no eeprom\n");
	}
	e1000_initmac(e);
	printk("[e1000]: mac addr=%x:%x:%x:%x:%x:%x\n", e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
	e->nic = net_nic_init(e, &e1000_nic_driver, e->mac, 6);
	e1000_startlink(e);

	for(int i=0;i<0x80;i++)
	    writecmd(e, 0x5200 + i*4, 0);

	e1000_rxinit(e);
	e1000_txinit(e);
	writecmd(e, REG_IMASK, 0x1F6DC);
	writecmd(e, REG_IMASK, 0xff & ~4);
	readcmd(e, 0xc0);
	e1000_startlink(e);
	net_nic_change(e->nic, NIC_CHANGE_UP);
	return 0;
}

static struct pci_driver pcidriver = {
	.init_device = _e1000_init_device,
	.name = "e1000",
	.num = 3,

	.supported = {
		{ INTEL_VEND, E1000_DEV },
		{ INTEL_VEND, E1000_I217 },
		{ INTEL_VEND, E1000_82577LM },
	},
};

__orderedinitializer(__orderedafter(PCI_INITIALIZER_ORDER + DEVICE_INITIALIZER_ORDER))
static void _init_e1000(void)
{
	linkedlist_create(&list, 0);
	pci_register_driver(&pcidriver);
}
