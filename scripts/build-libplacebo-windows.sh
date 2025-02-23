#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=v7.349.0
if [ ! -d "libplacebo" ]; then
git clone --recursive https://github.com/haasn/libplacebo.git || exit 1
fi
cd libplacebo || exit 1
git checkout $TAG || exit 1
git apply "${SCRIPT_DIR}/flatpak/0002-Vulkan-use-16bit-for-p010.patch" | exit 1
DIR=./build || exit 1
meson setup --prefix /mingw64 -Dxxhash=disabled $DIR || exit 1
ninja -C$DIR || exit 1
ninja -Cbuild install || exit 1
