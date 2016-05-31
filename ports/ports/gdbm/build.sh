NAME='gdbm'
VERSION='1.12'
DESC='GNU Database Manager'
REQUIRES=()

SOURCES=('ftp://ftp.gnu.org/gnu/gdbm/gdbm-1.12.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
