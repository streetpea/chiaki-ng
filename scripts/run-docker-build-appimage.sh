#!/bin/bash

set -xe
cd "`dirname $(readlink -f ${0})`"

docker build -t chiaki-bionic . -f Dockerfile.bionic
cd ..
docker run --rm \
	-v "`pwd`:/build/chiaki" \
	-w "/build/chiaki" \
	--device /dev/fuse \
	--cap-add SYS_ADMIN \
	-t chiaki-bionic \
	/bin/bash -c "scripts/build-appimage.sh"

