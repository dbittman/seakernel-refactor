NAME='bash'
VERSION='4.3'
DESC='Fast Lexical Analyzer'
REQUIRES=('ncurses' 'readline')

SOURCES=('http://ftp.gnu.org/gnu/bash/bash-4.3.tar.gz')
PATCHES=('bash-4.3-seaos.patch')
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --enable-curses --without-bash-malloc --enable-history --prefix=/ --enable-readline --with-installed-readline --disable-nls" "$STDMAKE" "$STDINSTALL"
}

