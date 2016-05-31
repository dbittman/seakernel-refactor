NAME='gawk'
VERSION='4.1.3'
DESC='GNU awk'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/gawk/gawk-4.1.3.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
