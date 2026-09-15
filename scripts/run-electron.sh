#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ELECTRON_DIR="$ROOT/wasm/electron"
WASM_DIR="$ROOT/build-wasm/wasm"
CMD="${1:-start}"

if [[ ! -f "$WASM_DIR/chiaki.wasm" ]]; then
	echo "WASM manquant. Compilez d'abord: ./scripts/build-wasm.sh" >&2
	echo "Attendu: $WASM_DIR/chiaki.wasm" >&2
	exit 1
fi

cd "$ELECTRON_DIR"
if [[ ! -d node_modules ]]; then
	echo "npm install (wasm/electron) ..."
	npm install
fi

echo "electron $CMD ..."
npm run "$CMD"
