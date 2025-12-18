#!/bin/bash

cd "`dirname $(readlink -f ${0})`/../.."

docker run --rm \
	-v "`pwd`:/build/chiaki":z \
	-w "/build/chiaki" \
	ghcr.io/streetpea/chiaki-build-switch:latest \
	/bin/bash -c "scripts/switch/build.sh"

if [[ $? -eq 0 ]]; then
	sudo chmod -R 777 ./build_switch
	cp build_switch/switch/chiaki.nro build_switch/switch/chiaki-ng.nro
fi

