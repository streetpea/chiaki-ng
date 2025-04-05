#!/bin/bash

set -xe
cd "`dirname $(readlink -f ${0})`"

podman build -t chiaki-qt6 . -f Dockerfile.qt6
cd ..
podman run --rm \
	--volume "`pwd`:/build/chiaki:Z" \
	-w "/build/chiaki" \
	--device /dev/fuse \
	--cap-add SYS_ADMIN \
	-t chiaki-qt6 \
	/bin/bash -c "sudo -E scripts/build-appimage.sh /build/appdir"

