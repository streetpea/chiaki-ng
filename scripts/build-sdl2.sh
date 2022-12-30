#!/bin/bash

set -xe

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

SDL_VER=2.26.1
URL=https://www.libsdl.org/release/SDL2-${SDL_VER}.tar.gz
FILE=SDL2-${SDL_VER}.tar.gz
DIR=SDL2-${SDL_VER}

if [ ! -d "$DIR" ]; then
	curl -L "$URL" -O
	tar -xf "$FILE"
fi

cd "$DIR" || exit 1

mkdir -p build && cd build || exit 1
cmake \
	-DCMAKE_INSTALL_PREFIX="$ROOT/sdl2-prefix" \
	-DSDL_ATOMIC=OFF \
	-DSDL_AUDIO=ON \
	-DSDL_CPUINFO=OFF \
	-DSDL_EVENTS=ON \
	-DSDL_FILE=OFF \
	-DSDL_FILESYSTEM=OFF \
	-DSDL_HAPTIC=ON \
	-DSDL_JOYSTICK=ON \
	-DSDL_LOADSO=ON \
	-DSDL_RENDER=OFF \
	-DSDL_SHARED=ON \
	-DSDL_STATIC=OFF \
	-DSDL_TEST=OFF \
	-DSDL_THREADS=ON \
	-DSDL_TIMERS=OFF \
	-DSDL_VIDEO=OFF \
	..
# SDL_THREADS is not needed, but it doesn't compile without

make -j4
make install

