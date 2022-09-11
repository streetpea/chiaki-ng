#!/bin/bash

# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

mkdir build && cd build || exit 1
cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	$CMAKE_EXTRA_ARGS \
	.. || exit 1
make -j4 || exit 1
test/chiaki-unit || exit 1

