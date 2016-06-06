NAME='mpc'
VERSION='1.0.3'
DESC='Multiprecision library'
REQUIRES=('mpfr' 'gmp')

SOURCES=('ftp://ftp.gnu.org/gnu/mpc/mpc-1.0.3.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF CC=$TARGET-gcc" "$STDMAKE" "$STDINSTALL"
}
