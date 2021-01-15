#!/bin/bash

set -xe

mkdir appimage

pip3 install --user protobuf
scripts/fetch-protoc.sh appimage
export PATH="`pwd`/appimage/protoc/bin:$PATH"
scripts/build-ffmpeg.sh appimage
scripts/build-sdl2.sh appimage

mkdir build_appimage
cd build_appimage 
cmake \
	-GNinja \
	-DCMAKE_BUILD_TYPE=Release \
	"-DCMAKE_PREFIX_PATH=`pwd`/../appimage/ffmpeg-prefix;`pwd`/../appimage/sdl2-prefix;/opt/qt512" \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_CLI=OFF \
	-DCHIAKI_ENABLE_GUI=ON \
	-DCHIAKI_ENABLE_SETSU=ON \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	-DCMAKE_INSTALL_PREFIX=/usr \
	..
cd ..
ninja -C build_appimage
build_appimage/test/chiaki-unit

DESTDIR=`pwd`/appimage/appdir ninja -C build_appimage install
cd appimage

curl -L -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-x86_64.AppImage
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-plugin-qt-x86_64.AppImage
set +e
source /opt/qt512/bin/qt512-env.sh
set -e

export LD_LIBRARY_PATH="`pwd`/sdl2-prefix/lib:$LD_LIBRARY_PATH"
export EXTRA_QT_PLUGINS=opengl

./linuxdeploy-x86_64.AppImage --appdir=appdir -e appdir/usr/bin/chiaki -d appdir/usr/share/applications/chiaki.desktop --plugin qt --output appimage
mv Chiaki-*-x86_64.AppImage Chiaki.AppImage
