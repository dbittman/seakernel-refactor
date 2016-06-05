#!/bin/sh
cd sysroot
LIST=$(find . -executable -type f -exec file {} \; | grep -i elf | sed -r 's/^([^:]+):.*$/\1/p')
for i in $LIST; do
	x86_64-elf-linux-musl-strip --strip-all $i
done
