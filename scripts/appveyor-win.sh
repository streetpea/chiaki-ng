#!/bin/bash

echo "APPVEYOR_BUILD_FOLDER=$APPVEYOR_BUILD_FOLDER"

mkdir ninja && cd ninja || exit 1
wget https://github.com/ninja-build/ninja/releases/download/v1.9.0/ninja-win.zip && 7z x ninja-win.zip || exit 1
cd .. || exit 1

mkdir yasm && cd yasm || exit 1
wget http://www.tortall.net/projects/yasm/releases/yasm-1.3.0-win64.exe && mv yasm-1.3.0-win64.exe yasm.exe || exit 1
cd .. || exit 1

export PATH="$PWD/ninja:$PWD/yasm:/c/Qt/5.12/msvc2017_64/bin:$PATH"

scripts/build-ffmpeg.sh . --target-os=win64 --arch=x86_64 --toolchain=msvc || exit 1

git clone https://github.com/xiph/opus.git && cd opus && git checkout ad8fe90db79b7d2a135e3dfd2ed6631b0c5662ab || exit 1
mkdir build && cd build || exit 1
cmake \
	-G Ninja \
	-DCMAKE_C_COMPILER=cl \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$APPVEYOR_BUILD_FOLDER/opus-prefix" \
	.. || exit 1
ninja || exit 1
ninja install || exit 1
cd ../.. || exit 1

wget https://mirror.firedaemon.com/OpenSSL/openssl-1.1.1l-dev.zip && 7z x openssl-1.1.1l-dev.zip || exit 1

wget https://www.libsdl.org/release/SDL2-devel-2.0.10-VC.zip && 7z x SDL2-devel-2.0.10-VC.zip || exit 1
export SDL_ROOT="$APPVEYOR_BUILD_FOLDER/SDL2-2.0.10" || exit 1
export SDL_ROOT=${SDL_ROOT//[\\]//} || exit 1
echo "set(SDL2_INCLUDE_DIRS \"$SDL_ROOT/include\")
set(SDL2_LIBRARIES \"$SDL_ROOT/lib/x64/SDL2.lib\")
set(SDL2_LIBDIR \"$SDL_ROOT/lib/x64\")" > "$SDL_ROOT/SDL2Config.cmake" || exit 1

mkdir protoc && cd protoc || exit 1
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.9.1/protoc-3.9.1-win64.zip && 7z x protoc-3.9.1-win64.zip || exit 1
cd .. || exit 1
export PATH="$PWD/protoc/bin:$PATH" || exit 1

PYTHON="C:/Python37/python.exe"
"$PYTHON" -m pip install protobuf || exit 1

QT_PATH="C:/Qt/5.12/msvc2017_64"

COPY_DLLS="$PWD/openssl-1.1/x64/bin/libcrypto-1_1-x64.dll $PWD/openssl-1.1/x64/bin/libssl-1_1-x64.dll $SDL_ROOT/lib/x64/SDL2.dll"

mkdir build && cd build || exit 1

cmake \
	-G Ninja \
	-DCMAKE_C_COMPILER=cl \
	-DCMAKE_C_FLAGS="-we4013" \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo \
	-DCMAKE_PREFIX_PATH="$APPVEYOR_BUILD_FOLDER/ffmpeg-prefix;$APPVEYOR_BUILD_FOLDER/opus-prefix;$APPVEYOR_BUILD_FOLDER/openssl-1.1/x64;$QT_PATH;$SDL_ROOT" \
	-DPYTHON_EXECUTABLE="$PYTHON" \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	.. || exit 1

ninja || exit 1

test/chiaki-unit.exe || exit 1

cd .. || exit 1


# Deploy

mkdir Chiaki && cp build/gui/chiaki.exe Chiaki || exit 1
mkdir Chiaki-PDB && cp build/gui/chiaki.pdb Chiaki-PDB || exit 1

"$QT_PATH/bin/windeployqt.exe" Chiaki/chiaki.exe || exit 1
cp -v $COPY_DLLS Chiaki
