#include <drivers/pci.h>

static int _rtl_init_device(struct pci_device *dev)
{

}

static struct pci_driver pcidriver = {
	.init_device = _rtl_init_device,
	.name = "rtl8139",
	.num = 1,

	.supported = {
		{ 0x10ec, 0x8139},
	},
};


