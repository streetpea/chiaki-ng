#!/bin/bash

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=1f1ba06aa2a83a2c75ade4150b7c0bba10531088
if [ ! -d "libplacebo" ]; then
git clone --recursive https://github.com/haasn/libplacebo.git || exit 1
fi
cd libplacebo || exit 1
git checkout $TAG || exit 1
git apply "${SCRIPT_DIR}/flatpak/0001-Vulkan-Don-t-try-to-reuse-old-swapchain.patch" || exit 1
git apply "${SCRIPT_DIR}/flatpak/0002-Vulkan-use-16bit-for-p010.patch" | exit 1
DIR=./build || exit 1
meson setup --prefix /usr -Dxxhash=disabled $DIR || exit 1
ninja -C$DIR || exit 1
ninja -C$DIR install || exit 1
