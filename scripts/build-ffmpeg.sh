#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=n7.1

git clone https://git.ffmpeg.org/ffmpeg.git --depth 1 -b $TAG && cd ffmpeg || exit 1
git apply ${SCRIPT_DIR}/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch || exit 1
./configure --enable-static --enable-avformat --enable-vulkan --enable-libv4l2 --enable-indev=v4l2 --enable-outdev=v4l2 --enable-libdrm --enable-avcodec --enable-v4l2_m2m --enable-decoder=h264 --enable-decoder=hevc --enable-hwaccel=h264_vaapi --enable-hwaccel=hevc_vaapi --enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan --prefix=/usr "$@" || exit 1
make -j4 || exit 1
make install || exit 1
