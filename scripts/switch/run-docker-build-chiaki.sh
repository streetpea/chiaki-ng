#!/bin/bash

cd "`dirname $(readlink -f ${0})`/../.."

docker run \
	-v "`pwd`:/build/chiaki" \
	-w "/build/chiaki" \
	-t \
	thestr4ng3r/chiaki-build-switch:35829cc \
	-c "scripts/switch/build.sh"

