#!/bin/bash

set -xe
cd "`dirname $(readlink -f ${0})`"

podman build -t chiaki-focal . -f Dockerfile.focal
cd ..
podman run --rm \
	-v "`pwd`:/build/chiaki" \
	-w "/build/chiaki" \
	--device /dev/fuse \
	--cap-add SYS_ADMIN \
	-t chiaki-focal \
	/bin/bash -c "scripts/build-appimage.sh /build/appdir"

