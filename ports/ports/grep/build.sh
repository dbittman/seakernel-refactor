NAME='grep'
VERSION='2.25'
DESC='Regular Expression Search Tool'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/grep/grep-2.25.tar.xz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
