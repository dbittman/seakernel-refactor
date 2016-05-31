NAME='findutils'
VERSION='4.6.0'
DESC='Find command, and associated stuff'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/findutils/findutils-4.6.0.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
