# Chiaki-NG Web Edition

**Browser + Electron** port of Chiaki-NG: `chiaki-lib` runs as WebAssembly, POSIX networking (TCP/UDP to the PS4/PS5) goes through a **Node.js proxy**.

Online web client (DEMO): [https://chiaki.csphere.fr/](https://chiaki.csphere.fr/)

Build: see [`Build_WASM.md`](Build_WASM.md).

---

## Setup

### 1. WASM + Node.js (web client / server)

The browser has no POSIX sockets. The page loads the UI (`wasm/www/`) and the WASM binary.
Node serves the files (**COOP/COEP** headers required for pthreads) and relays `/posix-net` over WebSocket to TCP/UDP.

Local launch: `node wasm/proxy/server.mjs` → http://127.0.0.1:8090/ (or the port from `.env`).

### 2. Electron client (Windows / Linux / macOS)

Same stack (WASM + proxy + UI) in a Chromium window. An `.exe` / AppImage / `.dmg` starts the proxy locally and opens the UI: no browser or server required.

- Sources: `wasm/electron/`
- Output: `wasm/build/`
- Commands: `.\scripts\build-electron.ps1` / `./scripts/build-electron.sh`

The **web server mode is not removed**. Electron does **not** read the server `.env`: it uses a `.env` in the user profile.

### 3. Node proxy — role and ports

The proxy does three things:

1. Serve the UI and `.wasm` with COOP/COEP (`SharedArrayBuffer`).
2. Relay each POSIX packet from WASM to the console (`ws://…/posix-net`).
3. APIs (`/api/…`): accounts, consoles, port check, sharing, etc.

**On the machine that hosts Node** (server or PC):

| Port | Usage |
|---|---|
| `CHIAKI_WASM_PORT` (**8090**) TCP | HTTP(S) + WebSocket UI and posix-net |
| **443** TCP | Public HTTPS (e.g. [chiaki.csphere.fr](https://chiaki.csphere.fr/)) via nginx / reverse-proxy |

Outside `localhost`, WASM pthreads require **HTTPS**.

**Router / firewall / console** (Remote Play, IPv4):

| Port | Proto | Role |
|---|---|---|
| **9295** | TCP | Remote Play session — **tested** before adding a console |
| **9296** / **9297** | UDP | Video / audio stream (open on NAT if you play outside LAN) |
| **987** | UDP | PS4 discovery (LAN) |
| **9302** | UDP | PS5 discovery (LAN) |

Without **TCP 9295** open through to the console, manual add is rejected. On LAN, discovery + 9295 is often enough. On WAN, forward **9295 TCP** (and in practice the stream UDP ports) to the PS4/PS5.

Electron only exposes the proxy on `127.0.0.1` (port **18780** by default): no Internet-facing port to open for the exe itself. The console must still be reachable (LAN or 9295).

---

## Optimizations and additions

### `.env` file (server and Electron)

One format to configure Node **without recompiling**. Copy `.env.example` to `.env` at the repo root (for the server):

```env
CHIAKI_AUTH_ENABLED=true
CHIAKI_AUTH_ALLOW_REGISTER=true
CHIAKI_ADMIN_USER=
CHIAKI_ADMIN_EMAIL=
CHIAKI_ADMIN_PASSWORD=
CHIAKI_DB_DIR=./db
CHIAKI_DB_NAME=chiaki.sqlite
CHIAKI_SESSION_DAYS=30
CHIAKI_SESSION_SECRET=change-me
CHIAKI_MAX_HOSTS=32
CHIAKI_WASM_PORT=8090
```

Other useful variables: `CHIAKI_WASM_ROOT`, `CHIAKI_WASM_WWW`, `CHIAKI_WASM_BIND`, `CHIAKI_WASM_LAN_HTTPS`, `CHIAKI_DISCOVERY_ENABLED`, `CHIAKI_ENV_FILE`.

| | Server | Electron |
|---|---|---|
| File | `.env` at the project root (or `cwd`) | `%AppData%/Chiaki-NG Web/.env` (Windows) — **not** the server one |
| Auth | enable in production (`CHIAKI_AUTH_ENABLED=true`) | disabled by default (local use) |
| Port | 8080 / 8090 / 443 | 18780 locally only |

### Authentication

Protects access to registered consoles (SQLite). Sign-in / account creation, cookie sessions, optional admin account on first start. Consoles and settings follow the user across devices.

Disable auth (`CHIAKI_AUTH_ENABLED=false`): local / Electron use with no login page.

### Stream sharing (with auth)

A signed-in host can generate a **viewer link**. Optional rights:

- video
- audio
- physical gamepad
- the virtual pad follows the host stream

Guests receive the stream (WebRTC) without the console’s Remote Play keys.

### Keys and mouse

**Keys** tab: separate **keyboard** and **mouse** remaps (DualShock / DualSense buttons, sticks, triggers, PS, touchpad). Capture on click, reset to defaults, stream shortcuts (stop, restart, cursor / **F8**). Mouse: left or right stick, sensitivity, invert Y, cursor lock on the stream.

### Virtual pad (smartphone)

Touch overlay: short taps sent immediately, layout / size in a full-screen editor, transparency. Built for mobile; shown from the stream toolbar.

### Languages

**French** and **English** UI (General tab). Files in `wasm/www/i18n/`.

### Other

- Stream: WebCodecs H.264/H.265, AudioWorklet, IDR, WASM lazy-load, Android fullscreen, × = exit fullscreen only
- Stream defaults: **1080p**, **60 fps**, **15000 kbps**, H265
- Add console: IPv4, TCP 9295 check, PSN ID via [psntools](https://www.psntools.com/) (Look up button)
- LAN discovery, wake-up, PIN registration

---
