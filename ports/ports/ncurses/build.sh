NAME='ncurses'
VERSION='6.0'
DESC='Curses Library'
REQUIRES=()

SOURCES=('ftp://ftp.gnu.org/gnu/ncurses/ncurses-6.0.tar.gz')
PATCHES=('ncurses-6.0-seaos.patch')
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --with-shared --enable-widec" "$STDMAKE -j4" "$STDINSTALL"
}

