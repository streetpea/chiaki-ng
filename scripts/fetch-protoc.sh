#!/bin/bash

set -xe

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

if [ "$(uname -m)" = "aarch64" ]
then
    export ARCH_SUFFIX="aarch_64"
else
    export ARCH_SUFFIX="x86_64"
fi

URL=https://github.com/protocolbuffers/protobuf/releases/download/v3.9.1/protoc-3.9.1-linux-${ARCH_SUFFIX}.zip

curl -L "$URL" -o protoc.zip
unzip protoc.zip -d protoc

