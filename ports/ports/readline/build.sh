NAME='readline'
VERSION='6.3'
DESC='GNU readline library'
REQUIRES=('ncurses')

SOURCES=('ftp://ftp.gnu.org/gnu/readline/readline-6.3.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --enable-shared bash_cv_wcwidth_broken=no" "$STDMAKE" "$STDINSTALL"
}
