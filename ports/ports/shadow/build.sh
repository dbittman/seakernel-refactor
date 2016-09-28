NAME='shadow'
VERSION='4.2'
DESC='User Security Utilities'
REQUIRES=()

SOURCES=('http://pkg-shadow.alioth.debian.org/releases/shadow-4.2.tar.xz')
PATCHES=('shadow-4.2-seaos-all.patch')
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
	cd shadow-4.2
	./autogen.sh
	cd ..
}

build() {
	cp -r ../shadow-$VERSION/* .
	if ! ./configure $STDCONF ac_cv_func_posix_spawn=no --without-nscd --disable-subordinate-ids --disable-man --disable-selinux --disable-pam --without-pam; then
		return 1
	fi
	make -C lib
	make -C libmisc
	if ! make -C src; then
		return 1
	fi
	make -C src install $STDINSTALL -i
}
