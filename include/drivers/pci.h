#pragma once

#include <lib/linkedlist.h>
#include <slab.h>

struct pci_config_space
{
    /* 0x00 */
    uint16_t vendor_id;
    uint16_t device_id;
    /* 0x04 */
    uint16_t command;
    uint16_t status;
    /* 0x08 */
    uint16_t revision;
    uint8_t  subclass;
    uint8_t  class_code;
    /* 0x0C */
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    /* 0x10 */
    uint32_t bar0;
    /* 0x14 */
    uint32_t bar1;
    /* 0x18 */
    uint32_t bar2;
    /* 0x1C */
    uint32_t bar3;
    /* 0x20 */
    uint32_t bar4;
    /* 0x24 */
    uint32_t bar5;
    /* 0x28 */
    uint32_t cardbus_cis_pointer;
    /* 0x2C */
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    /* 0x30 */
    uint32_t expansion_rom_base_address;
    /* 0x34 */
    uint32_t reserved0;
    /* 0x38 */
    uint32_t reserved1;
    /* 0x3C */
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint8_t  min_grant;
    uint8_t  max_latency;
}__attribute__((packed));

struct pci_driver;
struct pci_device {
	struct kobj_header _header;
	int bus, dev, func;
	struct pci_config_space config;
	int flags;

	struct pci_driver *driver;
	
	struct linkedentry entry;
};

struct pci_driver {
	int (*init_device)(struct pci_device *);
	struct linkedentry entry;
	const char *name;

	int num;
	struct {
		uint16_t vendor;
		uint16_t device;
	} supported[];
};

void pci_register_driver(struct pci_driver *driver);
void pci_write_dword(const uint16_t bus, const uint16_t dev, const uint16_t func, const uint32_t reg, unsigned data);
#define PCI_INITIALIZER_ORDER 1
