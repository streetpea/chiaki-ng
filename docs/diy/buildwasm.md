# Compiler le client WASM

Le port navigateur compile `chiaki-lib` avec Emscripten. Un proxy Node.js local relaie TCP/UDP (le navigateur n’expose pas de sockets POSIX).

## Dépendances

- Emscripten SDK (`emcc`, `emcmake`)
- CMake, Ninja, Python 3, `protoc`
- Node.js 18+

Initialiser les submodules : `git submodule update --init --recursive`.

## Build

```bash
emcmake cmake -S . -B build-wasm -G Ninja -DCHIAKI_ENABLE_WASM=ON
cmake --build build-wasm --target chiaki-wasm
cd build-wasm/wasm
node server.mjs
```

Ouvrir `http://127.0.0.1:8080/`.

Détails, limites et architecture : voir `wasm/README.md`.
