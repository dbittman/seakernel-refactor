# -ffreestanding needed for kernel
CFLAGS=-ffreestanding -Wall -Wextra -Iinclude -std=gnu11 -Wshadow -Wstrict-overflow -fno-strict-aliasing -fno-omit-frame-pointer -g -include "include/_default.h"
ASFLAGS=
BUILDDIR=build
CONFIGFILE=config.cfg
LDFLAGS=
.DEFAULT_GOAL=all
# these are the required libraries that the kernel needs.
LIBRARIES=ds string
MAKEFILES=Makefile $(CONFIGFILE)
# read in configuration, and add to CFLAGS
include $(CONFIGFILE)
SYSROOT=$(CONFIG_BUILD_SYSROOT)
export SYSROOT
CFLAGS+=$(addprefix -D,$(shell cat $(CONFIGFILE) | sed -e 's/=y/=1/g' -e 's/=n/=0/g' -e 's/\#.*$$//' -e '/^$$/d'))

ARCH=$(CONFIG_ARCH)
MACHINE=$(CONFIG_MACHINE)

C_SOURCES=
ASM_SOURCES=

ifeq ($(CONFIG_UBSAN),y)
CFLAGS+=-fsanitize=undefined -fstack-protector-all -fstack-check
endif

# these warnings pop up if we define asserts to nothing, so remove them
ifeq ($(CONFIG_DEBUG),n)
CFLAGS+=-Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable
endif

ifeq ($(CONFIG_BUILD_WERROR),y)
CFLAGS+=-Werror
endif

ifeq ($(CONFIG_PERF_FUNCTIONS),y)
ifeq ($(CONFIG_UBSAN),y)
$(error Cannot do both UBSAN and PERF at the same time)
endif

CFLAGS+=-finstrument-functions -finstrument-functions-exclude-file-list=kernel/debug,serial,printk,lib/string,panic,arch
endif

ifeq ($(CONFIG_BUILD_LTO),y)
ifeq ($(CONFIG_BUILD_CLANG),y)
$(error Cannot do LTO with clang)
endif
$(warning Link Time Optimization (LTO) is untested and unsupported)
CFLAGS+=-flto
LDFLAGS+=-flto
endif

CFLAGS+=-O$(CONFIG_BUILD_OPTIMIZATION)

include $(SYSROOT)/usr/src/include.mk

all: $(BUILDDIR)/kernel.elf $(BUILDDIR)/initrd.tar $(USRPROGS)

# get all normal kernel sources
include kernel/include.mk

ARCHDIR=arch/$(ARCH)
MACHINEDIR=machine/$(MACHINE)

# include the machine sources and the arch sources
# MACHINE must come first!
include $(MACHINEDIR)/include.mk
include $(ARCHDIR)/include.mk

ifeq ($(CONFIG_BUILD_SYSTEM_COMPILER),y)
TOOLCHAIN_PREFIX=
else
TOOLCHAIN_PREFIX:=$(TOOLCHAIN_PREFIX)-
endif

ifeq ($(CONFIG_BUILD_CLANG),y)
CC=clang -target $(TOOLCHAIN_PREFIX) -i$(SYSROOT) /home/dbittman/toolchain/install
else
CC=$(TOOLCHAIN_PREFIX)gcc
endif

AS=$(TOOLCHAIN_PREFIX)as
LD=$(TOOLCHAIN_PREFIX)gcc

MAKEFILES+=$(MACHINEDIR)/include.mk $(ARCHDIR)/include.mk $(SYSROOT)/usr/src/include.mk

# for each library that we're using, include their sources
$(foreach lib,$(LIBRARIES),$(eval include lib/$(lib)/include.mk))

MAKEFILES+=$(addsuffix /include.mk,$(addprefix lib/,$(LIBRARIES)))



#drivers
include drivers/include.mk


STARTFILE=$(shell $(LD) -print-file-name=crtbegin.o)
ENDFILE=$(shell $(LD) -print-file-name=crtend.o)

CFLAGS+=-I$(ARCHDIR)/include -I$(MACHINEDIR)/include
C_OBJECTS=$(addprefix $(BUILDDIR)/,$(C_SOURCES:.c=.o))
ASM_OBJECTS=$(addprefix $(BUILDDIR)/,$(ASM_SOURCES:.S=.o))
SOURCES=$(C_SOURCES) $(ASM_SOURCES)

OBJECTS=
ifneq ($(CRTIOBJ),)
OBJECTS+=$(BUILDDIR)/$(CRTIOBJ)
endif
OBJECTS+=$(STARTFILE) $(sort $(C_OBJECTS) $(ASM_OBJECTS)) $(ENDFILE)
ifneq ($(CRTNOBJ),)
OBJECTS+=$(BUILDDIR)/$(CRTNOBJ)
endif
listobj:
	echo $(OBJECTS)
	echo $(USRPROGS)

# this is the final kernel binary, including the symbol table.
$(BUILDDIR)/kernel.elf: $(OBJECTS) $(BUILDDIR)/link.ld $(MAKEFILES) $(BUILDDIR)/symbols.o
	@echo -e "[LD]\t$(BUILDDIR)/kernel.elf"
	@$(LD) $(LDFLAGS) -Wl,-dT,$(BUILDDIR)/link.ld,--export-dynamic -o $(BUILDDIR)/kernel.elf -nostdlib $(OBJECTS) -lgcc $(BUILDDIR)/symbols.o

# this is the generation of symbols.c, from its parts.
$(BUILDDIR)/symbols.c: kernel/syms.c.begin kernel/syms.c.end $(BUILDDIR)/kernel.sym.c
	@echo -e "[GEN]\t$(BUILDDIR)/symbols.c"
	@cat kernel/syms.c.begin $(BUILDDIR)/kernel.sym.c kernel/syms.c.end > $(BUILDDIR)/symbols.c
	@echo >> $(BUILDDIR)/symbols.c
	@echo 'size_t kernel_symbol_table_length = ' $$(wc -l < $(BUILDDIR)/kernel.sym.c) ';' >> $(BUILDDIR)/symbols.c

# ...and compile symbols.c
$(BUILDDIR)/symbols.o: $(BUILDDIR)/symbols.c
	@echo -e "[CC]\t$(BUILDDIR)/symbols.o"
	@$(CC) $(CFLAGS) -c -o $(BUILDDIR)/symbols.o $(BUILDDIR)/symbols.c

# here we dump the symbol table from the stage1 link, and process it into array initializers
# for C using sed and friends.
# this is one line because it's a memorial to the pain and suffering caused by how stupid
# the developers of binutils are.
$(BUILDDIR)/kernel.sym.c: $(BUILDDIR)/kernel.elf.stage1
	@echo -e "[GEN]\t$(BUILDDIR)/kernel.sym.c"
	@$(TOOLCHAIN_PREFIX)objdump -t $(BUILDDIR)/kernel.elf.stage1 | grep "^.* [lg]" | awk '{print $$1 " " $$(NF-1) " " $$NF}' | grep -v ".hidden" | sed -rn 's|([0-9a-f]+) ([0-9a-f]+) ([a-zA-Z0-9_/\.]+)|{.value=0x\1, .size=0x\2, .name="\3"},|p' > $(BUILDDIR)/kernel.sym.c

# link the stage1 kernel binary, not including the symbol table
$(BUILDDIR)/kernel.elf.stage1: $(OBJECTS) $(BUILDDIR)/link.ld $(MAKEFILES)
	@echo -e "[LD]\t$(BUILDDIR)/kernel.elf.stage1"
	@$(LD) $(LDFLAGS) -Wl,-dT,$(BUILDDIR)/link.ld,--export-dynamic -o $(BUILDDIR)/kernel.elf.stage1 -nostdlib $(OBJECTS) -lgcc

.PHONY: lint clean test all flags

lint:
	cppcheck --enable=style,warning,performance,portability,information,missingInclude -I include -I $(MACHINEDIR)/include -I $(ARCHDIR)/include . -I lib/fdt/include -i lib/fdt

autotest:
	@./tests/build-opts-test.sh "" "-enable-kvm" "-smp 2" "-smp 4 -enable-kvm"

-include $(OBJECTS:.o=.d)

$(BUILDDIR)/initrd.tar: $(USRPROGS)
	@echo -e "[TAR]\t$(BUILDDIR)/initrd.tar"
	@-rm $(BUILDDIR)/initrd.tar 2>/dev/null
	@tar --format=ustar -C initrd -c -f $(BUILDDIR)/initrd.tar .

$(BUILDDIR)/hd.img: $(USRPROGS)
	truncate -s 1GB $(BUILDDIR)/hd.img
	mke2fs -F $(BUILDDIR)/hd.img
	mkdir -p $(BUILDDIR)/mnt
	sudo mount $(BUILDDIR)/hd.img $(BUILDDIR)/mnt
	sudo cp -r $(SYSROOT)/* $(SYSROOT)-install/* $(BUILDDIR)/mnt/
	sudo mkdir -p $(BUILDDIR)/mnt/bin
	sudo mkdir -p $(BUILDDIR)/mnt/dev
	sudo mkdir -p $(BUILDDIR)/mnt/tmp
	sudo cp $(USRPROGS) $(BUILDDIR)/mnt/bin
	sudo mkdir -p $(BUILDDIR)/mnt/usr/src/seakernel
	sudo cp -r arch drivers include kernel lib machine tests tools config.cfg Makefile README.md $(BUILDDIR)/mnt/usr/src/seakernel
	sudo sed -s -i -e 's|CONFIG_BUILD_SYSROOT=.*|CONFIG_BUILD_SYSROOT=/|g' $(BUILDDIR)/mnt/usr/src/seakernel/config.cfg
	sudo sed -s -i -e 's|CONFIG_BUILD_SYSTEM_COMPILER=.*|CONFIG_BUILD_SYSTEM_COMPILER=y|g' $(BUILDDIR)/mnt/usr/src/seakernel/config.cfg
	sudo sed -s -i -e 's|CONFIG_BUILD_CLANG=.*|CONFIG_BUILD_CLANG=n|g' $(BUILDDIR)/mnt/usr/src/seakernel/config.cfg
	sudo umount $(BUILDDIR)/mnt

newhd:
	-rm $(BUILDDIR)/hd.img
	$(MAKE) $(BUILDDIR)/hd.img

QEMU_AHCI=-device ahci,id=ahci0 -drive if=none,file=$(BUILDDIR)/hd.img,format=raw,id=drive-sata0-0-0 -device ide-drive,bus=ahci0.0,drive=drive-sata0-0-0,id=sata0-0-0

test: $(BUILDDIR)/kernel.elf $(USRPROGS) $(BUILDDIR)/initrd.tar $(BUILDDIR)/hd.img
	qemu-system-$(ARCH) -m 1024 -machine $(MACHINE) $(QEMU_FLAGS) -kernel $(BUILDDIR)/kernel.elf -serial stdio -initrd $(BUILDDIR)/initrd.tar $(QEMU_AHCI)

clean: clean_progs
	-rm -r $(BUILDDIR)

flags:
	echo $(CFLAGS)
	echo $(LDFLAGS)

$(BUILDDIR)/%.o: %.c $(MAKEFILES)
	@mkdir -p $(@D)
	@echo -e "[CC]\t$@"
	@$(CC) $(CFLAGS) -c -MD -MF $(BUILDDIR)/$*.d -o $@ $<
	@mv -f $(BUILDDIR)/$*.d $(BUILDDIR)/$*.d.tmp
	@sed -e 's|.*:|$@:|' < $(BUILDDIR)/$*.d.tmp > $(BUILDDIR)/$*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.d.tmp | fmt -1 | \
		sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.d
	@rm -f $(BUILDDIR)/$*.d.tmp

$(BUILDDIR)/%.o: %.S $(MAKEFILES)
	@mkdir -p $(@D)
	@echo -e "[AS]\t$@"
	@$(CC) $(CFLAGS) -c -MD -MF $(BUILDDIR)/$*.d -o $@ $<
	@mv -f $(BUILDDIR)/$*.d $(BUILDDIR)/$*.d.tmp
	@sed -e 's|.*:|$@:|' < $(BUILDDIR)/$*.d.tmp > $(BUILDDIR)/$*.d
	@sed -e 's/.*://' -e 's/\\$$//' < $(BUILDDIR)/$*.d.tmp | fmt -1 | \
		sed -e 's/^ *//' -e 's/$$/:/' >> $(BUILDDIR)/$*.d
	@rm -f $(BUILDDIR)/$*.d.tmp

