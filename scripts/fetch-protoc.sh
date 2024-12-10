#!/bin/bash

set -xe

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

if [ "$(uname -m)" = "aarch64" ]
then
  URL='https://github.com/protocolbuffers/protobuf/releases/download/v29.1/protoc-29.1-linux-aarch_64.zip'
else
  URL='https://github.com/protocolbuffers/protobuf/releases/download/v29.1/protoc-29.1-linux-x86_64.zip'
fi

curl -L "$URL" -o protoc.zip
unzip protoc.zip -d protoc

