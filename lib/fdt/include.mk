include lib/fdt/Makefile.libfdt
MAKEFILES+=lib/fdt/Makefile.libfdt
C_SOURCES+=$(addprefix lib/fdt/, $(LIBFDT_SRCS))
CFLAGS+=-Ilib/fdt/include
