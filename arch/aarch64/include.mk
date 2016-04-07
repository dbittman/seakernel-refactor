TOOLCHAIN_PREFIX=aarch64-elf

CFLAGS+=-mcpu=cortex-a57 -mlittle-endian -mgeneral-regs-only 
ASFLAGS+=-mcpu=cortex-a57 -mlittle-endian

ARCH_SUPPORTED=FEATURE_SUPPORTED_UNWIND FEATURE_SUPPORTED_CYCLE_COUNT

QEMU_FLAGS+=-cpu cortex-a57 

ASM_SOURCES+= \
	$(ARCHDIR)/kernel/start.S \
	$(ARCHDIR)/kernel/exception.S \
	$(ARCHDIR)/kernel/switch.S \
	$(ARCHDIR)/kernel/calls.S
C_SOURCES+= \
	$(ARCHDIR)/kernel/interrupt.c \
	$(ARCHDIR)/kernel/init.c \
	$(ARCHDIR)/kernel/processor.c \
	$(ARCHDIR)/kernel/unwind.c \
	$(ARCHDIR)/kernel/mmu.c \
	$(ARCHDIR)/kernel/crti.c \
	$(ARCHDIR)/kernel/crtn.c \
	$(ARCHDIR)/kernel/thread.c \
	$(ARCHDIR)/devices/gic.c \
	$(ARCHDIR)/devices/serial.c \
	$(ARCHDIR)/devices/timer.c \
	$(ARCHDIR)/devices/psci.c \
	$(ARCHDIR)/devices/uart.c


# these are defined by machine's include.mk
CFLAGS+=-DKERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET) -DKERNEL_PHYS_BASE=$(KERNEL_PHYS_BASE) -DKERNEL_VIRT_BASE=$(KERNEL_VIRT_BASE) $(addprefix -D,$(ARCH_SUPPORTED))

$(BUILDDIR)/link.ld: $(ARCHDIR)/link.ldi
	@echo -e "[GEN]\t$(BUILDDIR)/link.ld"
	@sed "s/%KERNEL_VIRT_BASE%/$(KERNEL_VIRT_BASE)/g" < $(ARCHDIR)/link.ldi | sed "s/%KERNEL_PHYS_BASE%/$(KERNEL_PHYS_BASE)/g" | sed "s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/g" > $(BUILDDIR)/link.ld
