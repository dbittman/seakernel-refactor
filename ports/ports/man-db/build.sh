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
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL -i"
}
