#!/bin/sh
set -e
mkdir -p toolchain

(
cd toolchain
ARCHES=x86_64 SYSROOT=../sysroot ../toolchain.sh .
)

(
cd ports
SYSROOT=../sysroot ./build.sh $(ls ports)
)

