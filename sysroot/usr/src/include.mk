
USRPROGS_NAMES=cond init syslogd sctl ps thr pswitch udps

USRPROGS=$(addprefix initrd/bin/,$(USRPROGS_NAMES))

initrd/bin/udps: $(SYSROOT)/usr/src/udps.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $<

initrd/bin/thr: $(SYSROOT)/usr/src/thr.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $< -Wall 

initrd/bin/init: $(SYSROOT)/usr/src/init.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $< -Wall 

initrd/bin/ps: $(SYSROOT)/usr/src/ps.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $< -Wall 

initrd/bin/sctl: $(SYSROOT)/usr/src/sctl.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $< -Wall 

initrd/bin/syslogd: $(SYSROOT)/usr/src/syslogd.c
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)linux-musl-gcc -static -O2 -g  -o $@ $< -Wall 

include $(SYSROOT)/usr/src/cond/include.mk
MAKEFILES+=$(SYSROOT)/usr/src/cond/include.mk

clean_progs:
	-rm $(USRPROGS)

