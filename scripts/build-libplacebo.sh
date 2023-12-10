#!/bin/bash

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=v6.338.1
if [ ! -d "libplacebo" ]; then
git clone --recursive https://github.com/haasn/libplacebo.git -b $TAG || exit 1
fi
cd libplacebo || exit 1
DIR=./build || exit 1
meson $DIR || exit 1
ninja -C$DIR || exit 1
ninja -Cbuild install || exit 1
