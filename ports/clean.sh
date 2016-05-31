#!/bin/sh
if [[ "$1" == "" ]]; then
	echo Specify port to clean
	exit 1
fi

rm -rf build/$1 2> /dev/null

