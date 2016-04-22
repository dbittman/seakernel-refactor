#include <drivers/pci.h>
#include <x86_64-ioport.h>
#include <printk.h>
#include <system.h>
static bool ready = false;
static struct linkedlist drivers;
static struct linkedlist devices;

static struct kobj kobj_pci_device = KOBJ_DEFAULT(pci_device);

static const char * class_code[13] = 
{ 
"Legacy", "Mass Storage Controller", "Network Controller", 
"Video Controller", "Multimedia Unit", "Memory Controller", "Bridge",
"Simple Communications Controller", "Base System Peripheral", "Input Device",
"Docking Station", "Processor", "Serial Bus Controller"
};

static const char * subclass[13][8] = 
{
{ "Legacy", "VGA", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "SCSI", "IDE", "Floppy", "IPI", "RAID", "Other", "Other", "Other" },
{ "Ethernet", "Token ring", "FDDI", "ATM", "Other", "Other", "Other", "Other" },
{ "VGA", "XGA", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "Video", "Audio", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "RAM", "Flash", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "Host", "ISA", "EISA", "MCA", "PCI-PCI", "PCMCIA", "NuBus", "CardBus" },
{ "Serial", "Parallel", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "PIC", "DMA", "PIT", "RTC", "Other", "Other", "Other", "Other" },
{ "Keyboard", "Digitizer", "Mouse", "Other", "Other", "Other", "Other", "Other" },
{ "Generic", "Other", "Other", "Other", "Other", "Other", "Other", "Other" },
{ "386", "486", "Pentium", "Other", "Other", "Other", "Other", "Other" },
{ "Firewire", "ACCESS", "SSA", "USB", "Other", "Other", "Other", "Other" }
};

static uint32_t pci_read_dword(const uint16_t bus, const uint16_t dev, 
	const uint16_t func, const uint32_t reg)
{
	x86_64_outl(0xCF8, 0x80000000L | ((uint32_t)bus << 16) |((uint32_t)dev << 11) |
	((uint32_t)func << 8) | (reg & ~3));
	return x86_64_inl(0xCFC + (reg & 3));
}

void pci_write_dword(const uint16_t bus, const uint16_t dev, 
	const uint16_t func, const uint32_t reg, unsigned data)
{
	x86_64_outl(0xCF8, 0x80000000L | ((uint32_t)bus << 16) |((uint32_t)dev << 11) |
	((uint32_t)func << 8) | (reg & ~3));
	x86_64_outl(0xCFC + (reg & 3), data);
}

static bool get_pci_config(struct pci_device *dev)
{
	/* Get the dword and parse the vendor and device ID */
	unsigned tmp = pci_read_dword(dev->bus, dev->dev, dev->func, 0);
	unsigned short vendor = (tmp & 0xFFFF);
	if(vendor && vendor != 0xFFFF) {
		/* Valid device! Okay, so the config space is 256 bytes long
		 * and we read in dwords: 64 reads should do it.
		 */
		int i;
		for(i=0;i<64;i+=16)
		{
			*(uint32_t*)((uintptr_t)&dev->config + i) =      pci_read_dword(dev->bus, dev->dev, dev->func, i);
			*(uint32_t*)((uintptr_t)&dev->config + i + 4) =  pci_read_dword(dev->bus, dev->dev, dev->func, i + 4);
			*(uint32_t*)((uintptr_t)&dev->config + i + 8) =  pci_read_dword(dev->bus, dev->dev, dev->func, i + 8);
			*(uint32_t*)((uintptr_t)&dev->config + i + 12) = pci_read_dword(dev->bus, dev->dev, dev->func, i + 12);
		}
		if(dev->config.class_code < 13 && dev->config.subclass != 0x80) {
			printk("[pci]: [%3.3d:%2.2d:%d] Vendor %4.4x, Device %4.4x, cc %2.2x, sc %2.2x: %s %s\n", 
				dev->bus, dev->dev, dev->func, dev->config.vendor_id, dev->config.device_id, dev->config.class_code, dev->config.subclass,
				subclass[dev->config.class_code][dev->config.subclass], 
				class_code[dev->config.class_code]);
		}
		return true;
	}
	return false;
}

static void pci_scan(void)
{
	if(!ready)
		return;
	__linkedlist_lock(&devices);
	__linkedlist_lock(&drivers);

	struct linkedentry *entry, *drventry;
	for(entry = linkedlist_iter_start(&devices);
			entry != linkedlist_iter_end(&devices);
			entry = linkedlist_iter_next(entry)) {
		for(drventry = linkedlist_iter_start(&drivers);
				drventry != linkedlist_iter_end(&drivers);
				drventry = linkedlist_iter_next(drventry)) {
			
			struct pci_device *device = linkedentry_obj(entry);
			struct pci_driver *driver = linkedentry_obj(drventry);

			for(int i=0;i<driver->num;i++) {
				if(device->config.vendor_id == driver->supported[i].vendor
						&& device->config.device_id == driver->supported[i].device) {
					if(device->driver == NULL) {
						device->driver = driver;
						device->flags = driver->init_device(device);
					}
				}
			}
		}
	}

	__linkedlist_unlock(&drivers);
	__linkedlist_unlock(&devices);
}

static void _late_init(void)
{
	ready = true;
	pci_scan();
}

__orderedinitializer(PCI_INITIALIZER_ORDER) static void _init_pci(void)
{
	linkedlist_create(&drivers, 0);
	linkedlist_create(&devices, 0);

	for(int bus=0;bus<256;bus++)
	{
		for(int dev=0;dev<32;dev++)
		{
			for(int func = 0; func < 8; func++)
			{
				struct pci_device *device = kobj_allocate(&kobj_pci_device);
				device->bus = bus;
				device->dev = dev;
				device->func = func;
				device->driver = NULL;
				device->flags = 0;
				if(!get_pci_config(device)) {
					kobj_putref(device);
				} else {
					linkedlist_insert(&devices, &device->entry, device);
				}
			}
		}
	}
	init_register_late_call(&_late_init, NULL);
}

void pci_register_driver(struct pci_driver *driver)
{
	linkedlist_insert(&drivers, &driver->entry, driver);
	pci_scan();
}
