#!/bin/sh


if [[ "$1" == "" ]]; then
	echo Specify a list of packages to download
	exit 1
fi

if [[ "$ARCH" == "" ]]; then
	echo Specify \$ARCH
	exit 1
fi

if [[ "$SYSROOT" == "" ]]; then
	echo Specify a sysroot.
	exit 1
fi

SYSROOT=$(realpath $SYSROOT)

REPO=http://googoo-16.ssrc.ucsc.edu/dbittman/repo/$ARCH

TMPDIR=$(mktemp -d -t seaos-repo-download.XXXXXXXXXX) || exit 1

install_package() {
	set -e
	grep "^$1:" repo.dat | tail -1
	if [[ "${PIPESTATUS[0]}" != "0" ]]; then
		echo $1 was not found!
		exit 0
	fi

	LINE=$(grep "^$1:" repo.dat | tail -1)
	IFS=: read NAME VERSION REQ <<< "$LINE"

	for i in $REQ; do
		install_package $i
	done

	if [ -e $SYSROOT/usr/packs/installed/$NAME-$VERSION.manifest ]; then
		echo $1 already installed
		return 0
	fi

	if ! curl --fail $REPO/pkg/$NAME-$VERSION.tar.xz > $NAME-$VERSION.tar.xz; then
		echo failed to download $1
		exit 1
	fi
	echo installing $NAME-$VERSION
	mkdir -p $NAME-$VERSION
	(
		set -e
		cd $NAME-$VERSION
		tar xf ../$NAME-$VERSION.tar.xz
	)
	cp -a $NAME-$VERSION/root/* $SYSROOT/
	cp $NAME-$VERSION/*.manifest $SYSROOT/usr/packs/installed/$NAME-$VERSION.manifest
	
	return 0
}

(
	# exit the subshell on error, but still clean up
	set -e
	cd $TMPDIR
	curl $REPO/repo.dat > repo.dat
	for pkg in $@; do
		install_package $pkg
	done
)

echo cleaning up...
rm -rf $TMPDIR

