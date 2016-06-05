NAME='gcc'
VERSION='5.3.0'
DESC='GNU Compiler Collection'
REQUIRES=('mpc' 'mpfr' 'gmp')

SOURCES=('http://mirrors.concertpass.com/gcc/releases/gcc-5.3.0/gcc-5.3.0.tar.bz2')
PATCHES=('gcc-5.3.0-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --enable-host-shared --enable-shared enable_shared=yes --enable-languages=c,c++ --enable-lto --disable-nls --disable-werror --target=$TARGET --with-build-sysroot=$SYSROOT" "all-gcc all-target-libgcc all-target-libstdc++-v3" "install-gcc install-target-libgcc install-target-libstdc++-v3 $STDDESTDIR"
}
