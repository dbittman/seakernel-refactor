NAME='bison'
VERSION='3.0.4'
DESC='Parser generator'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/bison/bison-3.0.4.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
