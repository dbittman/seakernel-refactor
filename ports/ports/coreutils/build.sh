NAME='coreutils'
VERSION='8.25'
DESC='GNU Core System Utilities'
REQUIRES=()

SOURCES=('ftp://ftp.gnu.org/gnu/coreutils/coreutils-8.25.tar.xz')
PATCHES=('coreutils-8.25-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL -i"
}
