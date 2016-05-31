NAME='bash'
VERSION='4.3'
DESC='Fast Lexical Analyzer'
REQUIRES=('ncurses')

SOURCES=('http://ftp.gnu.org/gnu/bash/bash-4.3.tar.gz')
PATCHES=()
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --without-bash-malloc --with-curses --enable-history --prefix=/ --enable-readline" "$STDMAKE" "$STDINSTALL"
}

