# Build WASM et Electron

Toujours depuis la **racine du dépôt** :

```bash
git submodule update --init --recursive
```

Les deux cibles sont indépendantes. Electron a **besoin du WASM déjà compilé** (`build-wasm/wasm/chiaki.wasm`).

---

## 1. WASM (navigateur / SERVEUR)

Sortie : `build-wasm/wasm/` — lancer avec `node wasm/proxy/server.mjs` puis http://127.0.0.1:8080/

### À installer

| | Windows | Linux |
|---|---|---|
| Git | oui | `sudo apt install git` |
| [Emscripten (emsdk)](https://emscripten.org/docs/getting_started/downloads.html) | `emcc` / `emcmake` | idem |
| CMake + Ninja + Python 3 | oui (le script crée `tools\wasm-python` avec protobuf 4.25.3 pour nanopb) | `sudo apt install cmake ninja-build python3` |
| `protoc` | téléchargé par le script dans `tools\protoc\` (pas dans git) | `sudo apt install protobuf-compiler` (ou le script le télécharge) |
| Node.js ≥ 18 | pour **lancer** le serveur | `sudo apt install nodejs` |

**Emscripten (une fois)**

Windows (PowerShell) :

```powershell
git clone https://github.com/emscripten-core/emsdk.git $env:USERPROFILE\emsdk
cd $env:USERPROFILE\emsdk
.\emsdk install latest
.\emsdk activate latest
```

Linux :

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install latest
./emsdk activate latest
source ~/emsdk/emsdk_env.sh
```

### Compiler

Windows :

```powershell
.\scripts\build-wasm.ps1
```

Linux (emsdk chargé dans le PATH) :

```bash
source ~/emsdk/emsdk_env.sh
chmod +x scripts/build-wasm.sh
./scripts/build-wasm.sh
```

### Lancer (SERVEUR / local)

```bash
node wasm/proxy/server.mjs
```

Port : `CHIAKI_WASM_PORT` (défaut `8080`).

---

## 2. Electron (.exe / AppImage)

Empaque le WASM + le proxy Node + l’UI dans une appli desktop. **Ne remplace pas** le mode SERVEUR.

Sortie : `wasm/build/pack-…/win-unpacked/chiaki-ng-web.exe` (Windows) ou AppImage (Linux).

### À installer

- Tout le **WASM** ci-dessus, **déjà compilé**
- Node.js ≥ 18 + npm (le script fait `npm install` dans `wasm/electron/`)


### Compiler

Windows :

```powershell
.\scripts\build-wasm.ps1
.\scripts\build-electron.ps1
```

Linux :

```bash
source ~/emsdk/emsdk_env.sh
./scripts/build-wasm.sh
chmod +x scripts/build-electron.sh
./scripts/build-electron.sh
```

Dev :

```powershell
.\scripts\run-electron.ps1
```

```bash
./scripts/run-electron.sh
```

macOS : `./scripts/build-electron.sh mac`
