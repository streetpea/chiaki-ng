#!/bin/bash

set -xe
	  
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

SDL_VER=3.4.0
SDL_COMPAT_VER=2.32.64
URL=https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL3-${SDL_VER}.tar.gz
FILE=SDL3-${SDL_VER}.tar.gz
DIR=SDL3-${SDL_VER}

if [ ! -d "$DIR" ]; then
	curl -L "$URL" -O
	tar -xf "$FILE"
fi

cd "$DIR" || exit 1
INSTALL_PREFIX="${INSTALL_PREFIX:=/usr}"
cmake \
    -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DSDL_TESTS=OFF \
	-DSDL_EXAMPLES=OFF

cmake --build build --config RelWithDebInfo --clean-first
cmake --install build --prefix "$INSTALL_PREFIX"
cd ..
URL=https://github.com/libsdl-org/sdl2-compat/releases/download/release-${SDL_COMPAT_VER}/sdl2-compat-${SDL_COMPAT_VER}.tar.gz
FILE=sdl2-compat-${SDL_COMPAT_VER}.tar.gz
DIR2=sdl2-compat-${SDL_COMPAT_VER}

if [ ! -d "$DIR2" ]; then
	curl -L "$URL" -O
	tar -xf "$FILE"
fi

cd "$DIR2" || exit 1
INSTALL_PREFIX="${INSTALL_PREFIX:=/usr}"
cmake \
    -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
	-DSDL2COMPAT_TESTS=OFF \

cmake --build build --config RelWithDebInfo --clean-first
cmake --install build --prefix "$INSTALL_PREFIX"
