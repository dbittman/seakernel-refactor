NAME='libressl'
VERSION='2.4.2'
DESC='Crypto Library'
REQUIRES=()

SOURCES=('http://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-2.4.2.tar.gz')
PATCHES=('libressl-2.4.2-seaos.patch')
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
