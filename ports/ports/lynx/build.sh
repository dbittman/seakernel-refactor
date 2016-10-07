NAME='lynx'
VERSION='2.8.8'
DESC='Text-based web browser'
REQUIRES=()

SOURCES=('http://invisible-mirror.net/archives/lynx/tarballs/lynx2.8.8rel.2.tar.bz2')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
	rm -rf lynx-$VERSION
	mv lynx2-8-8 lynx-2.8.8
}

build() {
	standard_build "$STDCONF --enable-ipv6 cf_cv_getaddrinfo=yes" "$STDMAKE" "$STDINSTALL"
}
