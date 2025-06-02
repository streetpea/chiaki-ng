#!/bin/bash

set -xe

if [ "$(uname -m)" = "aarch64" ]
then
    export GCC_STRING="gcc_arm64"
else
    export GCC_STRING="gcc_64"
fi

export QT_DIR="$(find ${QT_PATH} -maxdepth 1 -type d -name "${QT_VERSION}")"
export PATH="${QT_DIR}/${GCC_STRING}/bin:$PATH"
if [ -f "${HOME}/chiaki-venv/bin/activate" ]
then
   source "${HOME}/chiaki-venv/bin/activate"
fi

# sometimes there are errors in linuxdeploy in docker/podman when the appdir is on a mount
appdir=${1:-`pwd`/appimage/appdir}

rm -rf appimage && mkdir -p appimage

scripts/fetch-protoc.sh appimage
export PATH="`pwd`/appimage/protoc/bin:$PATH"
scripts/build-ffmpeg.sh appimage
scripts/build-sdl2.sh appimage
scripts/build-libplacebo.sh appimage
scripts/build-hidapi.sh appimage

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

export ARCH="$(uname -m)"
curl -L -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage
chmod +x linuxdeploy-${ARCH}.AppImage

export LD_LIBRARY_PATH="${QT_DIR}/${GCC_STRING}/lib:$(pwd)/../build_appimage/third-party/cpp-steam-tools:$LD_LIBRARY_PATH"
export QML_SOURCES_PATHS="$(pwd)/../gui/src/qml"
export EXTRA_QT_MODULES="waylandclient;waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqeglfs.so;libqminimal.so;libqminimalegl.so;libqvkkhrdisplay.so;libqvnc.so;libqoffscreen.so;libqlinuxfb.so"
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage
chmod +x linuxdeploy-plugin-qt-${ARCH}.AppImage
./linuxdeploy-${ARCH}.AppImage \
    --appdir="${appdir}" \
    -e "${appdir}/usr/bin/chiaki" \
    -d "${appdir}/usr/share/applications/chiaking.desktop" \
    --plugin qt \
    --exclude-library='libva*' \
    --exclude-library='libvulkan*' \
    --output appimage
mv chiaki-ng-${ARCH}.AppImage chiaki-ng.AppImage