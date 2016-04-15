#!/bin/sh
GCCVER=5.3.0
GCCSRC=ftp://ftp.gnu.org/gnu/gcc/gcc-${GCCVER}/gcc-${GCCVER}.tar.bz2
BINVER=2.26
BINSRC=ftp://ftp.gnu.org/gnu/binutils/binutils-${BINVER}.tar.bz2
ARCHES="x86_64 aarch64"
JOBS=4
set -e

if [[ "$1" == "" ]]; then
	echo First argument must be a path to install toolchain to
	exit 1
fi

if ! mkdir -p "$1"; then
	echo Failed to create target directory
	exit 1
fi


echo Downloading GCC...
curl $GCCSRC > $(basename $GCCSRC)
echo Downloading bintuls...
curl $BINSRC > $(basename $BINSRC)

echo Extracting sources...

tar xf $(basename $GCCSRC)
tar xf $(basename $BINSRC)

(
	set -e
	cd $(basename -s .tar.bz2 $GCCSRC)
	patch -p1 ../tools/gcc-$(GCCVER)-seaos.patch
)

export PREFIX="$(realpath "$1")"
export PATH="$PREFIX/bin:$PATH"

echo Installing to $PREFIX

for ARCH in $ARCHES; do
	export TARGET=$ARCH-elf
	mkdir -p build-$TARGET-{gcc,binutils}
	echo binutils: $TARGET
	(
		set -e
		cd build-$TARGET-binutils
		../$(basename -s .tar.bz2 $BINSRC)/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
		make -j $JOBS
		make install
	)
	echo gcc: $TARGET
	(
		set -e
		cd build-$TARGET-gcc
		../$(basename -s .tar.bz2 $GCCSRC)/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
		make -j $JOBS all-gcc
		make -j $JOBS CFLAGS_FOR_TARGET='-g -O2 -mcmodel=large' all-target-libgcc
		make install-gcc
		make install-target-libgcc
	)
done

echo Success!

