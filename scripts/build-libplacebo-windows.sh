#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG="${LIBPLACEBO_VERSION:-3be579e6c7f6b421d9ac3ab4860edc437c50b3eb}"
if [ ! -d "libplacebo" ]; then
git clone --recursive https://github.com/haasn/libplacebo.git || exit 1
fi
cd libplacebo || exit 1
git checkout $TAG || exit 1
DIR=./build || exit 1
meson setup --prefix /mingw64 -Dxxhash=disabled $DIR || exit 1
ninja -C$DIR || exit 1
ninja -Cbuild install || exit 1
