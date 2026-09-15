# Build WASM and Electron

Always from the **repository root**:

```bash
git submodule update --init --recursive
```

The two targets are independent. Electron **needs WASM already built** (`build-wasm/wasm/chiaki.wasm`).

---

## 1. WASM (browser / server)

Output: `build-wasm/wasm/` — run with `node wasm/proxy/server.mjs` then http://127.0.0.1:8080/

### Install

| | Windows | Linux |
|---|---|---|
| Git | yes | `sudo apt install git` |
| [Emscripten (emsdk)](https://emscripten.org/docs/getting_started/downloads.html) | `emcc` / `emcmake` | same |
| CMake + Ninja + Python 3 | yes (the script creates `tools\wasm-python` with protobuf 4.25.3 for nanopb) | `sudo apt install cmake ninja-build python3` |
| `protoc` | downloaded by the script into `tools\protoc\` (not in git) | `sudo apt install protobuf-compiler` (or the script downloads it) |
| Node.js ≥ 18 | to **run** the server | `sudo apt install nodejs` |

**Emscripten (once)**

Windows (PowerShell):

```powershell
git clone https://github.com/emscripten-core/emsdk.git $env:USERPROFILE\emsdk
cd $env:USERPROFILE\emsdk
.\emsdk install latest
.\emsdk activate latest
```

Linux:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install latest
./emsdk activate latest
source ~/emsdk/emsdk_env.sh
```

### Build

Windows:

```powershell
.\scripts\build-wasm.ps1
```

Linux (emsdk loaded in PATH):

```bash
source ~/emsdk/emsdk_env.sh
chmod +x scripts/build-wasm.sh
./scripts/build-wasm.sh
```

### Run (server / local)

```bash
node wasm/proxy/server.mjs
```

Port: `CHIAKI_WASM_PORT` (default `8080`).

---

## 2. Electron (.exe / AppImage)

Packages WASM + the Node proxy + the UI into a desktop app. **Does not replace** server mode.

Output: `wasm/build/pack-…/win-unpacked/chiaki-ng-web.exe` (Windows) or AppImage (Linux).

### Install

- All of the **WASM** above, **already built**
- Node.js ≥ 18 + npm (the script runs `npm install` in `wasm/electron/`)


### Build

Windows:

```powershell
.\scripts\build-wasm.ps1
.\scripts\build-electron.ps1
```

Linux:

```bash
source ~/emsdk/emsdk_env.sh
./scripts/build-wasm.sh
chmod +x scripts/build-electron.sh
./scripts/build-electron.sh
```

Dev:

```powershell
.\scripts\run-electron.ps1
```

```bash
./scripts/run-electron.sh
```

macOS: `./scripts/build-electron.sh mac`
