NAME='gcc'
VERSION='5.3.0'
DESC='GNU Compiler Collection'
REQUIRES=()

SOURCES=('http://www.netgull.com/gcc/releases/gcc-5.3.0/gcc-5.3.0.tar.bz2')
PATCHES=('gcc-5.3.0-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --enable-languages=c,c++ --disable-nls --disable-werror" "$STDMAKE" "$STDINSTALL"
}
