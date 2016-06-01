NAME='groff'
VERSION='1.22.3'
DESC='GNU Troff'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/groff/groff-1.22.3.tar.gz')
PATCHES=('groff-1.22.3-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --with-doc=no" "$STDMAKE" "$STDINSTALL"
}
