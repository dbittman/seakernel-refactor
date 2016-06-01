NAME='gmp'
VERSION='6.1.0'
DESC='Library for arbitrary precision math'
REQUIRES=()

SOURCES=('https://gmplib.org/download/gmp/gmp-6.1.0.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
