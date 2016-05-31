NAME='libpipeline'
VERSION='1.4.1'
DESC='Pipelining library'
REQUIRES=()

SOURCES=('http://download.savannah.gnu.org/releases/libpipeline/libpipeline-1.4.1.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
