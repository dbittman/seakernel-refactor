NAME='autoconf'
VERSION='2.69'
DESC='GNU autotools, autoconf'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.xz')
PATCHES=('autoconf-2.69-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
