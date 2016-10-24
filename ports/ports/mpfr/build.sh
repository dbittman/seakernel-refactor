NAME='mpfr'
VERSION='3.1.5'
DESC='C library for multiple-precision floating-point computations'
REQUIRES=('gmp')

SOURCES=('http://www.mpfr.org/mpfr-current/mpfr-3.1.5.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF CC=$TARGET-gcc" "$STDMAKE" "$STDINSTALL"
}
