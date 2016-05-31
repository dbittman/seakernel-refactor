NAME='automake'
VERSION='1.15'
DESC='GNU autotools, automake'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/automake/automake-1.15.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
