#!/bin/sh

if [[ "$1" == "" ]]; then
	echo Specify URL
	exit 1
fi

if [[ "$2" == "" ]]; then
	echo Specify DESC
	exit 1
fi

NAME=$(basename $1 | sed -rns 's/(\w+)-(.*)\.tar.*/\1/p')
VER=$(basename $1 | sed -rns 's/(\w+)-(.*)\.tar.*/\2/p')
mkdir -p ports/$NAME
cat > ports/$NAME/build.sh <<EOF
NAME='$NAME'
VERSION='$VER'
DESC='$2'
REQUIRES=()

SOURCES=('$1')
PATCHES=()
ALLFILES=${PATCHES[@]}

prepare() {
	standard_prepare
}

build() {
	standard_build "\$STDCONF" "\$STDMAKE" "\$STDINSTALL"
}
EOF

