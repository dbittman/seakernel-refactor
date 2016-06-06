NAME='man-db'
VERSION='2.7.5'
DESC='Man page viewer'
REQUIRES=()

SOURCES=('http://download.savannah.gnu.org/releases/man-db/man-db-2.7.5.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	export PKG_CONFIG_PATH=$SYSROOT:$SYSROOT/usr
	export PKG_CONFIG_LIBPATH=$SYSROOT/usr/lib
	standard_build "$STDCONF libpipeline_CFLAGS= libpipeline_LIBS=" "$STDMAKE" "$STDINSTALL -i"
}
