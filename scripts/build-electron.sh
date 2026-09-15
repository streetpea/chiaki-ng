#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELECTRON_DIR="$ROOT/wasm/electron"
OUT_DIR="$ROOT/wasm/build"
WASM_DIR="$ROOT/build-wasm/wasm"
WWW_DIR="$ROOT/wasm/www"
PROXY_DIR="$ROOT/wasm/proxy"
TARGET="${1:-current}"

if [[ ! -f "$WASM_DIR/chiaki.wasm" ]]; then
	echo "WASM manquant. Compilez d'abord: ./scripts/build-wasm.sh" >&2
	echo "Attendu: $WASM_DIR/chiaki.wasm" >&2
	exit 1
fi

if [[ "$TARGET" = "current" ]]; then
	case "$(uname -s)" in
		Linux*) TARGET=linux ;;
		Darwin*) TARGET=mac ;;
		MINGW*|MSYS*|CYGWIN*) TARGET=win ;;
		*) echo "OS non reconnu, précisez: win | linux | mac" >&2; exit 1 ;;
	esac
fi

case "$TARGET" in
	win) EB_OS=--win ;;
	linux) EB_OS=--linux ;;
	mac) EB_OS=--mac ;;
	*) echo "Cible invalide: $TARGET (win | linux | mac)" >&2; exit 1 ;;
esac

cd "$ELECTRON_DIR"
if [[ ! -d node_modules ]]; then
	echo "npm install (wasm/electron) ..."
	npm install
fi

mkdir -p "$OUT_DIR"
pkill -f "chiaki-ng-web" 2>/dev/null || true
taskkill //F //IM chiaki-ng-web.exe 2>/dev/null || true

STAGE="$ELECTRON_DIR/.stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
find "$WASM_DIR" -maxdepth 1 -type f -name 'chiaki*' -exec cp -f {} "$STAGE/" \;
cp -a "$WWW_DIR"/. "$STAGE/"
cp -a "$PROXY_DIR"/. "$STAGE/"
if grep -q "s-stream-auto" "$STAGE/index.html"; then
	echo "UI stage encore ancienne (s-stream-auto). Verifiez wasm/www/index.html" >&2
	exit 1
fi

STAMP="$(date +%Y%m%d-%H%M%S)"
PACK_NAME="pack-$STAMP"
PACK_REL="../build/$PACK_NAME"
PACK_ABS="$OUT_DIR/$PACK_NAME"

echo "Packaging Electron ($TARGET) -> $PACK_ABS"
npx --yes electron-builder "$EB_OS" --publish never --config.directories.output="$PACK_REL"
find "$PACK_ABS" -maxdepth 1 -type f -exec cp -f {} "$OUT_DIR/" \;
echo "OK. Installateurs dans: $OUT_DIR"
ls -1 "$OUT_DIR" || true
