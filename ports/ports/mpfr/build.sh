NAME='mpfr'
VERSION='3.1.4'
DESC='C library for multiple-precision floating-point computations'
REQUIRES=('gmp')

SOURCES=('http://www.mpfr.org/mpfr-current/mpfr-3.1.4.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
