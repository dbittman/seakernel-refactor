KERNEL_LOAD_OFFSET:=0x400000
KERNEL_PHYS_BASE:=0x0
KERNEL_VIRT_BASE:=0xFFFFFFFF80000000

C_SOURCES+=machine/pc/machine.c \
		   machine/pc/devices/serial.c \
		   machine/pc/devices/acpi.c \
		   machine/pc/devices/vga.c \
		   machine/pc/devices/ioapic.c \
		   machine/pc/devices/uart.c

ifneq ($(CONFIG_ARCH),x86_64)
$(error Machine 'pc' only supports x86_64, not $(CONFIG_ARCH))
endif
