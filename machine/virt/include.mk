KERNEL_LOAD_OFFSET:=0x10000
KERNEL_PHYS_BASE:=0x40000000
KERNEL_VIRT_BASE:=0xFFFF000000000000

C_SOURCES+=$(MACHINEDIR)/kernel/init.c
LIBRARIES+=fdt

ifneq ($(CONFIG_ARCH),aarch64)
ifneq ($(CONFIG_ARCH),arm)
$(error Machine 'virt' only supports arm and aarch64, not $(CONFIG_ARCH))
endif
endif
