#!/bin/sh
GCCVER=5.3.0
GCCSRC=ftp://ftp.gnu.org/gnu/gcc/gcc-${GCCVER}/gcc-${GCCVER}.tar.bz2
BINVER=2.26
BINSRC=ftp://ftp.gnu.org/gnu/binutils/binutils-${BINVER}.tar.bz2
JOBS=4
SYSROOT=$(realpath $SYSROOT)
SRCDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
set -e

if [[ "$ARCHES" == "" ]]; then
	echo Specify ARCHES.
	exit 1
fi

if [[ "$SYSROOT" == "" ]]; then
	echo Specify SYSROOT.
	exit 1
fi


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
echo Downloading binutils...
curl $BINSRC > $(basename $BINSRC)

echo Extracting sources...

tar xf $(basename $GCCSRC)
tar xf $(basename $BINSRC)

(
	set -e
	cd $(basename -s .tar.bz2 $GCCSRC)
	patch -p1 < $SRCDIR/tools/gcc-$GCCVER-seaos.patch
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

for ARCH in $ARCHES; do
	mkdir -p musl
	(
		cd musl
		rm -rf $ARCH
		git clone http://github.com/dbittman/musl-seaos $ARCH
		(
			cd $ARCH
			CROSS_COMPILE=$ARCH-elf- ./configure --prefix=$SYSROOT/usr --target=$ARCH-elf
			make -j4
			make install
		)
	)
done

for ARCH in $ARCHES; do
	export TARGET=$ARCH-elf-linux-musl
	mkdir -p build-$TARGET-hosted-{gcc,binutils}
	echo binutils: $TARGET
	(
		set -e
		cd build-$TARGET-hosted-binutils
		../$(basename -s .tar.bz2 $BINSRC)/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot=$SYSROOT --disable-nls --disable-werror
		make -j $JOBS
		make install
	)
	echo gcc: $TARGET
	(
		set -e
		cd build-$TARGET-hosted-gcc
		../$(basename -s .tar.bz2 $GCCSRC)/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --with-sysroot=$SYSROOT --disable-multilib
		make -j $JOBS all-gcc
		make -j $JOBS all-target-libgcc
		make install-gcc
		make install-target-libgcc
	)
done

for ARCH in $ARCHES; do
	mkdir -p musl
	(
		cd musl
		(
			cd $ARCH
			make distclean
			CROSS_COMPILE=$ARCH-elf-linux-musl- ./configure --prefix=$SYSROOT/usr --target=$ARCH-elf-linux-musl
			make -j4
			make install
		)
	)

	(
		set -e
		cd build-$ARCH-elf-linux-musl-hosted-gcc
		make -j $JOBS all-target-libstdc++-v3
		make -j $JOBS install-target-libstdc++-v3
	)

done

echo Success!

