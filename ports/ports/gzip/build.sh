NAME='gzip'
VERSION='1.8'
DESC='GNU zip'
REQUIRES=()

SOURCES=('http://mirrors.kernel.org/gnu/gzip/gzip-1.8.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
