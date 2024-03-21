#!/bin/bash

set -xe

export PATH="${QT_PATH}/${QT_VERSION}/gcc_64/bin:$PATH"

# sometimes there are errors in linuxdeploy in docker/podman when the appdir is on a mount
appdir=${1:-`pwd`/appimage/appdir}

rm -rf appimage && mkdir -p appimage

scripts/fetch-protoc.sh appimage
export PATH="`pwd`/appimage/protoc/bin:$PATH"
scripts/build-ffmpeg.sh appimage
scripts/build-sdl2.sh appimage
scripts/build-libplacebo.sh appimage

rm -rf build_appimage && mkdir -p build_appimage
cd build_appimage 
qt-cmake \
	-GNinja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_GUI=ON \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	-DCMAKE_INSTALL_PREFIX=/usr \
	..
cd ..

# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

ninja -C build_appimage
build_appimage/test/chiaki-unit

DESTDIR="${appdir}" ninja -C build_appimage install
cd appimage

curl -L -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-x86_64.AppImage
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-plugin-qt-x86_64.AppImage

export LD_LIBRARY_PATH="${QT_PATH}/${QT_VERSION}/gcc_64/lib:$(pwd)/../build_appimage/third-party/cpp-steam-tools:$LD_LIBRARY_PATH"
export QML_SOURCES_PATHS="$(pwd)/../gui/src/qml"

./linuxdeploy-x86_64.AppImage \
    --appdir="${appdir}" \
    -e "${appdir}/usr/bin/chiaki" \
    -d "${appdir}/usr/share/applications/chiaki4deck.desktop" \
    --plugin qt \
    --exclude-library='libva*' \
    --exclude-library='libvulkan*' \
    --exclude-library='libhidapi*' \
    --output appimage

mv chiaki4deck-x86_64.AppImage Chiaki4deck.AppImage
