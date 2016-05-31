NAME='flex'
VERSION='2.6.0'
DESC='Fast Lexical Analyzer'
REQUIRES=('m4')

SOURCES=('http://dbittman.github.io/mirror/flex-2.6.0.tar.bz2')
PATCHES=('flex-2.6.0-seaos.patch')
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}

