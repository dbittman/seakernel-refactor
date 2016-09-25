NAME='rsync'
VERSION='3.1.2'
DESC='Remote File Transfer'
REQUIRES=()

SOURCES=('https://download.samba.org/pub/rsync/src/rsync-3.1.2.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
