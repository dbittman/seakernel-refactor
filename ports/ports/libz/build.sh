NAME='libz'
VERSION='1.2.8.2015.12.26'
DESC='Port of zlib for Sortix.'
REQUIRES=()

SOURCES=('https://sortix.org/libz/release/libz-1.2.8.2015.12.26.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
