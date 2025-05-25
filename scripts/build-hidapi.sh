#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG="hidapi-0.15.0"
if [ ! -d "hidapi" ]; then
git clone --recursive https://github.com/libusb/hidapi.git || exit 1
fi
cd hidapi || exit 1
git checkout $TAG || exit 1
DIR=./build || exit 1
cmake  -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_PREFIX="/usr" \
       -DHIDAPI_WITH_LIBUSB=FALSE \
       -S . \
       -B $DIR
cmake --build $DIR --config Release || exit 1
cmake --install $DIR || exit 1