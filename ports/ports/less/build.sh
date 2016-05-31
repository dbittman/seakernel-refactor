NAME='less'
VERSION='481'
DESC='Terminal pager'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/less/less-481.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
