NAME='dropbear'
VERSION='2016.74'
DESC='SSH'
REQUIRES=()

SOURCES=('https://matt.ucc.asn.au/dropbear/dropbear-2016.74.tar.bz2')
PATCHES=('dropbear-2016.74-seaos-all.patch')
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	cp -rf ../$NAME-$VERSION/* .
	if ! ./configure $STDCONF; then
		return 1
	fi
	if ! make $STDMAKE; then
		return 1
	fi
	if ! make $STDINSTALL; then
		return 1
	fi

	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}

