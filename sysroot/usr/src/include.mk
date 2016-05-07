
USRPROGS_NAMES=cond init syslogd sctl ps thr

USRPROGS=$(addprefix initrd/bin/,$(USRPROGS_NAMES))

initrd/bin/thr: sysroot/usr/src/thr.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

initrd/bin/init: sysroot/usr/src/init.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

initrd/bin/ps: sysroot/usr/src/ps.c
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

