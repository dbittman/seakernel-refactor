#!/bin/bash

TARGET="x86_64-elf-linux-musl"

STDCONF="--prefix=/usr --host=$TARGET"
STDMAKE=

SYSROOT=$(realpath $SYSROOT)

if [[ "$SYSROOT" == "" ]]; then
	echo Specify a sysroot.
	exit 1
fi

contains_element () {
	local e
	for e in "${@:2}"; do [[ "$e" == "$1" ]] && return 0; done
	return 1
}

download_all() {
	for i in $@; do
		if ! curl -L $i > $(basename $i); then
			echo Downloading $i failed
			return 1
		fi
	done
	return 0
}

untar_all() {
	for i in $@; do
		echo Extracting $(basename $i)...
		if ! tar xf $(basename $i); then
			echo Extracting $(basename $i) failed
			return 1
		fi
	done
	return 0
}

apply_patches() {
	for i in $@; do
		if patch -p0 -N --dry-run --silent < $i 2>/dev/null; then
			if ! patch -p0 < $i; then
				echo Patching using $i failed
				return 1
			fi
		fi
	done
	return 0
}

standard_prepare() {
	if ! download_all $SOURCES; then
		return 1
	fi
	if ! untar_all $SOURCES; then
		return 1
	fi
	if ! apply_patches $PATCHES; then
		return 1
	fi
}

standard_build() {
	if ! ../$NAME-$VERSION/configure $1; then
		return 1
	fi
	if ! make $2; then
		return 1
	fi
	if ! make $3; then
		return 1
	fi
}

completed=()

do_package() {
	REQUIRES=()
	PATCHES=()
	SOURCES=()
	source ports/$1/build.sh
	for i in ${REQUIRES[@]}; do
		echo "--- Installing $i as depend for $1"
		do_package $i
	done
	source ports/$1/build.sh
	mkdir -p build/
	cp -r ports/$1 build/
	cd build/$1
	STDDESTDIR="DESTDIR=$(pwd)/install-$TARGET/root"
	STDINSTALL="install DESTDIR=$(pwd)/install-$TARGET/root"
	echo Preparing $1...
	if ! prepare > build-$TARGET.log; then
		cd ../..
		return 1
	fi
	mkdir -p build-$TARGET
	cd build-$TARGET
	echo Building $1...
	if ! build >> ../build-$TARGET.log 2>&1; then
		cd ../../..
		return 1
	fi
	completed+=("$1")

	cd ../install-$TARGET
	(find -name '*.la' | xargs rm) 2>/dev/null
	echo Building manifest for $1...
	find root | cut -c 5- > $NAME-$VERSION.manifest

	echo Installing $1...
	mkdir -p $SYSROOT
	cp -ar root/* $SYSROOT
	mkdir -p $SYSROOT/usr/packs/src
	mkdir -p $SYSROOT/usr/packs/installed/
	cp $NAME-$VERSION.manifest $SYSROOT/usr/packs/installed/

	cd ../../../
	cp -ar ports/$1 $SYSROOT/usr/packs/src

	return 0
}

for pk in $@; do
	if ! contains_element "$pk" "${completed[@]}"; then
		if ! do_package $pk; then
			echo FAILED TO BUILD $pk
			echo already completed packages: ${completed[@]}
			exit 1
		fi
	fi
done

echo SUCCESSFUL PACKAGES
echo ${completed[@]}

