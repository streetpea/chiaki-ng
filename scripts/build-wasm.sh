#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-build-wasm}"
cd "$ROOT"

if ! command -v emcmake >/dev/null; then
	echo "emcmake introuvable. Installez Emscripten (emsdk)." >&2
	exit 1
fi

PROTOC_DIR="$ROOT/tools/protoc/bin"
PROTOC_BIN="$PROTOC_DIR/protoc"
VER="3.9.1"
NEED=1
if [ -x "$PROTOC_BIN" ] && "$PROTOC_BIN" --version 2>/dev/null | grep -q "3.9.1"; then
	NEED=0
fi
if [ "$NEED" -eq 1 ]; then
	if [ "$(uname -m)" = "aarch64" ]; then ARCH="aarch_64"; else ARCH="x86_64"; fi
	URL="https://github.com/protocolbuffers/protobuf/releases/download/v${VER}/protoc-${VER}-linux-${ARCH}.zip"
	DEST="$ROOT/tools/protoc"
	ZIP="$(mktemp).zip"
	echo "Telechargement de protoc ${VER} vers tools/protoc (ignore le protoc systeme) ..."
	mkdir -p "$DEST"
	curl -L "$URL" -o "$ZIP"
	unzip -o "$ZIP" -d "$DEST"
	rm -f "$ZIP"
	chmod +x "$DEST/bin/protoc"
fi
export PATH="$PROTOC_DIR:$PATH"
if [ ! -x "$PROTOC_BIN" ]; then
	echo "protoc 3.9.1 introuvable dans tools/protoc/bin." >&2
	exit 1
fi

NANOPB_PB2="$ROOT/third-party/nanopb/generator/proto/nanopb_pb2.py"
if [ -f "$NANOPB_PB2" ] && grep -q runtime_version "$NANOPB_PB2"; then
	echo "Suppression de nanopb_pb2.py incompatible ..."
	rm -f "$NANOPB_PB2"
fi

VENV="$ROOT/tools/wasm-python"
if [ -x "$VENV/bin/python" ]; then
	WASM_PYTHON="$VENV/bin/python"
else
	echo "Creation de tools/wasm-python (protobuf 4.25.3, compatible nanopb) ..."
	python3 -m venv "$VENV"
	WASM_PYTHON="$VENV/bin/python"
fi
if ! "$WASM_PYTHON" -c "from google.protobuf.descriptor_pb2 import FileOptions; assert hasattr(FileOptions,'RegisterExtension')" 2>/dev/null; then
	echo "Installation de protobuf 4.25.3 dans tools/wasm-python ..."
	"$WASM_PYTHON" -m pip install --quiet --disable-pip-version-check "protobuf==4.25.3"
fi

emcmake cmake -S "$ROOT" -B "$ROOT/$BUILD_DIR" -G Ninja -DCHIAKI_ENABLE_WASM=ON -DPYTHON_EXECUTABLE="$WASM_PYTHON" -DPROTOC="$PROTOC_BIN"
cmake --build "$ROOT/$BUILD_DIR" --target chiaki-wasm
echo "OK. Lancer: node $ROOT/$BUILD_DIR/wasm/server.mjs"
echo "Puis ouvrir http://127.0.0.1:8080/"
