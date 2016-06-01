NAME='tar'
VERSION='1.29'
DESC='Tape Archiver'
REQUIRES=()

SOURCES=('http://mirrors.kernel.org/gnu/tar/tar-1.29.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
