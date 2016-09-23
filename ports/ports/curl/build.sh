NAME='curl'
VERSION='7.50.3'
DESC='Commandline data transfer via URLs'
REQUIRES=()

SOURCES=('https://curl.haxx.se/download/curl-7.50.3.tar.bz2')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
