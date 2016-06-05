NAME='binutils'
VERSION='2.26'
DESC='GNU Utilities for Working With Executable Files'
REQUIRES=('libz')

SOURCES=('http://ftp.gnu.org/gnu/binutils/binutils-2.26.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --enable-host-shared --with-system-zlib --enable-shared enable_shared=yes --disable-nls" "$STDMAKE" "$STDINSTALL"
}
