#!/bin/bash

set -xe
cd "`dirname $(readlink -f ${0})`"

podman build -t chiaki-bullseye . -f Dockerfile.bullseye
cd ..
podman run --rm -v "`pwd`:/build" chiaki-bullseye /bin/bash -c "
  cd /build &&
  rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py &&
  mkdir build_bullseye &&
  cmake -Bbuild_bullseye -GNinja -DCHIAKI_USE_SYSTEM_JERASURE=ON -DCHIAKI_USE_SYSTEM_NANOPB=ON &&
  ninja -C build_bullseye &&
  ninja -C build_bullseye test"

