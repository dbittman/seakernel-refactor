NAME='m4'
VERSION='1.4.17'
DESC='M4 Unix Macro System'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/m4/m4-1.4.17.tar.xz')
PATCHES=()
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE -j4" "$STDINSTALL"
}

