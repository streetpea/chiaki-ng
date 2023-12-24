#!/bin/bash

set -xveo pipefail


# borrowed from wiliwili for building
BASE_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0/"

PKGS=(
    "switch-dav1d-1.2.1-1-any.pkg.tar.zst"
    "switch-ffmpeg-6.1-2-any.pkg.tar.zst"
)
for PKG in "${PKGS[@]}"; do
    [ -f "${PKG}" ] || curl -LO ${BASE_URL}${PKG}
    dkp-pacman -U --noconfirm ${PKG}
done

# dkp-pacman -Sy
# dkp-pacman -S switch-dav1d switch-ffmpeg

arg1=$1
build="./build"
if [ "$arg1" != "linux" ]; then
	toolchain="cmake/switch.cmake"
	build="./build_switch"
fi

SCRIPTDIR=$(dirname "$0")
BASEDIR=$(realpath "${SCRIPTDIR}/../../")

build_chiaki (){
	pushd "${BASEDIR}"
		#rm -rf ./build
		echo "Base = ${BASEDIR}/build_switch}"
	    rm -rf build_switch
		# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
		rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

		cmake -B "${build}" \
			-GNinja \
			-DCMAKE_PREFIX_PATH=/opt/devkitpro/portlibs/switch/ \
			-DCHIAKI_ENABLE_SWITCH_CURL=ON \
			-DCHIAKI_ENABLE_SWITCH_JSONC=ON \
			-DCMAKE_TOOLCHAIN_FILE=${toolchain} \
			-DCHIAKI_ENABLE_TESTS=OFF \
			-DCHIAKI_ENABLE_CLI=OFF \
			-DCHIAKI_ENABLE_GUI=OFF \
			-DCHIAKI_ENABLE_ANDROID=OFF \
			-DCHIAKI_ENABLE_BOREALIS=ON \
			-DCHIAKI_LIB_ENABLE_MBEDTLS=ON \
			-DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF\
			-DCHIAKI_ENABLE_STEAM_SHORTCUT=OFF 
			#-DCMAKE_FIND_DEBUG_MODE=OFF 
			# 
			# -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
			# -DCMAKE_FIND_DEBUG_MODE=ON

		ninja -C "${build}"
	popd
}

build_chiaki

