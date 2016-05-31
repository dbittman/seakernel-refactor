NAME='make'
VERSION='4.2'
DESC='GNU make'
REQUIRES=()

SOURCES=('http://ftp.gnu.org/gnu/make/make-4.2.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF --without-guile" "$STDMAKE" "$STDINSTALL"
}
