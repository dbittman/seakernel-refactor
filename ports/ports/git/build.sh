NAME='git'
VERSION='2.10.0'
DESC='Version Control System'
REQUIRES=()

SOURCES=('https://github.com/git/git/archive/v2.10.0.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
	cd git-$VERSION
	autoconf
	cd ..
}

build() {
	cp -rf ../git-$VERSION/* .
	standard_build "$STDCONF ac_cv_fread_reads_directories=no ac_cv_snprintf_returns_bogus=no --with-editor=vim --with-pager=less --with-shell=sh --with-perl=perl --with-python=python" "$STDMAKE" "$STDINSTALL"
}
