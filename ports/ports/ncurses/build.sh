NAME='ncurses'
VERSION='6.0'
DESC='Curses Library'
REQUIRES=()

SOURCES=('ftp://ftp.gnu.org/gnu/ncurses/ncurses-6.0.tar.gz')
PATCHES=()
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE -j4" "$STDINSTALL"
}

