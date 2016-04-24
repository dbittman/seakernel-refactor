
USRPROGS_NAMES=cond init syslogd sctl

USRPROGS=$(addprefix initrd/bin/,$(USRPROGS_NAMES))

initrd/bin/init: sysroot/usr/src/init.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

initrd/bin/sctl: sysroot/usr/src/sctl.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

initrd/bin/syslogd: sysroot/usr/src/syslogd.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

include sysroot/usr/src/cond/include.mk
MAKEFILES+=sysroot/usr/src/cond/include.mk

clean_progs:
	-rm $(USRPROGS)

