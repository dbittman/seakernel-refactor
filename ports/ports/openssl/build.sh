NAME='openssl'
VERSION='1.1.0a'
DESC='SSL and Crypto Libraries'
REQUIRES=()

SOURCES=('https://www.openssl.org/source/openssl-1.1.0a.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	cp -rf ../$NAME-$VERSION/* .
	if ! ./Configure --prefix=/usr --cross-compile-prefix=$TARGET- -DOPENSSL_NO_ASYNC linux-$TARGETARCH; then
		return 1
	fi
	if ! make -j4; then
		return 1
	fi
	if ! make $STDINSTALL; then
		return 1
	fi
}
