
USRPROGS_NAMES=cond init syslogd test

USRPROGS=$(addprefix sysroot/bin/,$(USRPROGS_NAMES))

sysroot/bin/init: sysroot/usr/src/init.c
	@mkdir -p sysroot/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

sysroot/bin/test: sysroot/usr/src/test.c
	@mkdir -p sysroot/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

sysroot/bin/syslogd: sysroot/usr/src/syslogd.c
	@mkdir -p sysroot/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $< -Wall 

include sysroot/usr/src/cond/include.mk
MAKEFILES+=sysroot/usr/src/cond/include.mk

clean_progs:
	-rm -r sysroot/bin

