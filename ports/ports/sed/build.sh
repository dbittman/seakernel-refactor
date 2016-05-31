NAME='sed'
VERSION='4.2.2'
DESC='Stream editor'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/sed/sed-4.2.2.tar.gz')
PATCHES=('sed-4.2.2-seaos.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
