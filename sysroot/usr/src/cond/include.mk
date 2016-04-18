_COND_SRC=cond.c keyboard.c output.c
COND_SRC=$(addprefix sysroot/usr/src/cond/,$(_COND_SRC))
sysroot/bin/cond: $(COND_SRC)
	@mkdir -p sysroot/bin
	$(TOOLCHAIN_PREFIX)-linux-musl-gcc -static -Og -g  -o $@ $^ -Wall 

