#!/bin/sh

set -e

if [[ "$REPO" == "" ]]; then
	echo "Provide an output directory for the repo in \$REPO"
	exit 1
fi

if [[ "$ARCH" == "" ]]; then
	echo "Provide a target architecture in \$ARCH"
	exit 1
fi

if [[ "$REV" == "" ]]; then
	echo "Provide a revision in \$REV"
	exit 1
fi

if [[ "$1" == "" ]]; then
	echo "Provide a list of packages to package"
	exit 1
fi

package_program() {
	echo Packaging $i...
	source ports/$1/build.sh
	if [ -e $REPO/$ARCH/pkg/$NAME-VERSION-$REV.tar.xz ]; then
		echo ERROR: $NAME-$VERSION-$REV exists
		return 1
	fi
	echo $NAME:$VERSION-$REV:${REQUIRES[@]} >> $REPO/$ARCH/repo.dat
	tar -c -C build/$1/install-$ARCH-elf-linux-musl -f $REPO/$ARCH/pkg/$NAME-$VERSION-$REV.tar.xz .
	return 0
}

mkdir -p $REPO/$ARCH/pkg

for i in $@; do
	package_program $i
done

