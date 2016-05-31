NAME='diffutils'
VERSION='3.3'
DESC='Tools for making, comparing, and patching diffs'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/diffutils/diffutils-3.3.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
