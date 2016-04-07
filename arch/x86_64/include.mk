TOOLCHAIN_PREFIX=x86_64-elf

CFLAGS+=-mno-red-zone -mno-sse -mcmodel=kernel -mno-avx
LDFLAGS+=-mcmodel=kernel -Wl,-z,max-page-size=4096
# these are defined by machine's include.mk
CFLAGS+=-DKERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET) -DKERNEL_PHYS_BASE=$(KERNEL_PHYS_BASE) -DKERNEL_VIRT_BASE=$(KERNEL_VIRT_BASE) $(addprefix -D,$(ARCH_SUPPORTED))

ARCH_SUPPORTED=FEATURE_SUPPORTED_UNWIND FEATURE_SUPPORTED_CYCLE_COUNT

CRTIOBJ=arch/x86_64/kernel/crti.o
CRTNOBJ=arch/x86_64/kernel/crtn.o

ASM_SOURCES+=arch/x86_64/kernel/start.S \
			 arch/x86_64/kernel/gate.S \
			 arch/x86_64/kernel/bootstrap.S \
			 arch/x86_64/kernel/switch.S


C_SOURCES+=arch/x86_64/kernel/init.c \
		   arch/x86_64/kernel/mmu.c \
		   arch/x86_64/kernel/processor.c \
		   arch/x86_64/kernel/timer.c \
		   arch/x86_64/kernel/idt.c \
		   arch/x86_64/kernel/tss.c \
		   arch/x86_64/kernel/gdt.c \
		   arch/x86_64/kernel/unwind.c \
		   arch/x86_64/devices/apic.c \
		   arch/x86_64/devices/madt.c \
		   arch/x86_64/devices/hpet.c \
		   arch/x86_64/kernel/interrupt.c \
		   arch/x86_64/kernel/thread.c

$(BUILDDIR)/link.ld: $(ARCHDIR)/link.ldi $(MAKEFILES)
	@echo -e "[GEN]\t$(BUILDDIR)/link.ld"
	@sed "s/%KERNEL_VIRT_BASE%/$(KERNEL_VIRT_BASE)/g" < $(ARCHDIR)/link.ldi | sed "s/%KERNEL_PHYS_BASE%/$(KERNEL_PHYS_BASE)/g" | sed "s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/g" > $(BUILDDIR)/link.ld

