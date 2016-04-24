_COND_SRC=cond.c keyboard.c output.c
COND_SRC=$(addprefix sysroot/usr/src/cond/,$(_COND_SRC))
initrd/bin/cond: $(COND_SRC)
	@mkdir -p initrd/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $^ -Wall 

