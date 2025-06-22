#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=stable-1.30

git clone https://git.linuxtv.org/v4l-utils.git --depth 1 -b $TAG && cd v4l-utils || exit 1
meson setup --prefix=/usr build/ || exit 1
ninja -C build/ || exit 1
ninja -C build/ install || exit 1
mkdir -p /usr/include/linux
cp -r include/linux/* /usr/include/linux/