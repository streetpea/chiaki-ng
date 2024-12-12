#!/bin/bash

set -xe

if [ "$(uname -m)" = "aarch64" ]
then
    export GCC_STRING="gcc_arm64"
else
    export GCC_STRING="gcc_64"
fi

export PATH="${QT_PATH}/${QT_VERSION}/${GCC_STRING}/bin:$PATH"


# sometimes there are errors in linuxdeploy in docker/podman when the appdir is on a mount
appdir=${1:-`pwd`/appimage/appdir}

export PATH="`pwd`/appimage/protoc/bin:$PATH"
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
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage
chmod +x linuxdeploy-plugin-qt-${ARCH}.AppImage

export LD_LIBRARY_PATH="${QT_PATH}/${QT_VERSION}/${GCC_STRING}/lib:$(pwd)/../build_appimage/third-party/cpp-steam-tools:$LD_LIBRARY_PATH"
export QML_SOURCES_PATHS="$(pwd)/../gui/src/qml"
if [ "$(uname -m)" = "aarch64" ]
then
    curl -LO https://github.com/multiarch/qemu-user-static/releases/download/v7.2.0-1/qemu-aarch64-static
    chmod +x qemu-aarch64-static
    curl -LO https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-aarch64.AppImage
    chmod +x appimagetool-aarch64.AppImage
    ./qemu-aarch64-static ./linuxdeploy-${ARCH}.AppImage \
        --appdir="${appdir}" \
        -e "${appdir}/usr/bin/chiaki" \
        -d "${appdir}/usr/share/applications/chiaking.desktop" \
        --exclude-library='libva*' \
        --exclude-library='libvulkan*' \
        --exclude-library='libhidapi*'
    ./qemu-aarch64-static ./linuxdeploy-plugin-qt-${ARCH}.AppImage --appdir="${appdir}"
    ./qemu-aarch64-static ./appimagetool-aarch64.AppImage "${appdir}"
else
    ./linuxdeploy-${ARCH}.AppImage \
        --appdir="${appdir}" \
        -e "${appdir}/usr/bin/chiaki" \
        -d "${appdir}/usr/share/applications/chiaking.desktop" \
        --plugin qt \
        --exclude-library='libva*' \
        --exclude-library='libvulkan*' \
        --exclude-library='libhidapi*' \
        --output appimage
fi

mv chiaki-ng-${ARCH}.AppImage chiaki-ng.AppImage
