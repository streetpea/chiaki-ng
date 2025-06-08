#!/bin/bash

set -xe
	  
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

SDL_COMPAT_VER=2.32.56
# URL=https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL3-${SDL_VER}.tar.gz
# SDL_VER=3.2.16
# FILE=SDL3-${SDL_VER}.tar.gz
# DIR=SDL3-${SDL_VER}

# if [ ! -d "$DIR" ]; then
# 	curl -L "$URL" -O
# 	tar -xf "$FILE"
# fi
DIR=SDL
git clone https://github.com/libsdl-org/SDL ${DIR}

cd "$DIR" || exit 1
INSTALL_PREFIX="${INSTALL_PREFIX:=/usr}"
mkdir -p build && cd build || exit 1
cmake \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DCMAKE_PREFIX_PATH="$PWD/../../$DIR" \
	-DSDL_TESTS=OFF \
	-DSDL_EXAMPLES=OFF \
	..

make -j4
make install
cd ../..

URL=https://github.com/libsdl-org/sdl2-compat/releases/download/release-${SDL_COMPAT_VER}/sdl2-compat-${SDL_COMPAT_VER}.tar.gz
FILE=sdl2-compat-${SDL_COMPAT_VER}.tar.gz
DIR2=sdl2-compat-${SDL_COMPAT_VER}

if [ ! -d "$DIR2" ]; then
	curl -L "$URL" -O
	tar -xf "$FILE"
fi

cd "$DIR2" || exit 1
INSTALL_PREFIX="${INSTALL_PREFIX:=/usr}"
mkdir -p build && cd build || exit 1
cmake \
	-DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DCMAKE_PREFIX_PATH="$PWD/../../$DIR" \
	-DSDL2COMPAT_TESTS=OFF \
	..

make -j4
make install