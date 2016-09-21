NAME='openssh'
VERSION='7.3p1'
DESC='SSH Client and Server'
REQUIRES=()

SOURCES=('http://mirrors.sonic.net/pub/OpenBSD/OpenSSH/portable/openssh-7.3p1.tar.gz')
PATCHES=()
ALLFILES=

prepare() {
	standard_prepare
}

build() {
	standard_build "$STDCONF" "$STDMAKE" "$STDINSTALL"
}
