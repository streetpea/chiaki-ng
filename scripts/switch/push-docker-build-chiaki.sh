#!/bin/bash
# Usage: Open netload via hbmenu (press Y), then
# jingkai@jkwin:~/chiaki-ng/scripts/switch$ ./push-podman-build-chiaki.sh -a 192.168.1.35
# Sending chiaki-ng.nro, 22095181 bytes
# 10289946 sent (46.57%), 918 blocks
# starting server
# server active ...
# initNxLink
# [I] Parse config file chiaki.conf
#...
#[INFO] Gamepad detected: Nintendo Switch Controller

cd "`dirname $(readlink -f ${0})`/../.."

docker run \
    -v "`pwd`:/build/chiaki" \
    -w "/build/chiaki" \
    -ti -p 28771:28771 \
    --entrypoint /opt/devkitpro/tools/bin/nxlink \
    ghcr.io/streetpea/chiaki-build-switch:latest \
    "$@" -s /build/chiaki/build_switch/switch/chiaki-ng.nro

