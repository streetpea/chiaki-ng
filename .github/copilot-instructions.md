# Copilot Instructions for chiaki-ng

## Big picture
- `lib/` is the core Remote Play protocol implementation in C (session, discovery, stream/control, crypto, FEC, audio/video decode hooks). Most feature work should start here.
- `gui/` is a Qt6/QML desktop app that wraps `chiaki-lib` via C++ bridge objects.
  - `gui/src/qmlbackend.cpp` orchestrates app state, discovery, PSN token refresh, and session lifecycle.
  - `gui/src/streamsession.cpp` maps `Settings` + host credentials into `ChiakiConnectInfo`, initializes decoders/audio/haptics, and drives streaming callbacks.
  - `gui/src/discoverymanager.cpp` bridges `chiaki_discovery_service_*` callbacks to Qt signals.
- `cli/` is a small wrapper over `chiaki-lib` (`discover`, `wakeup`), also callable from GUI command mode (`gui/src/main.cpp`).
- `android/` builds against the same root `CMakeLists.txt` with platform-specific flags (GUI/CLI/FFmpeg disabled there).

## Build/test workflows used in this repo
- Always initialize submodules first: `git submodule update --init --recursive`.
- Typical desktop build (CMake):
  - `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`
  - `cmake --build build --target chiaki`
- Unit tests live in `test/` and build into `chiaki-unit`:
  - `cmake -S . -B build -DCHIAKI_ENABLE_TESTS=ON`
  - `cmake --build build --target chiaki-unit`
  - run `build/test/chiaki-unit` (this is how project scripts run tests).
- Packaging/build automation references:
  - AppImage path: `scripts/build-appimage.sh` (runs `build_appimage/test/chiaki-unit`).
  - CI matrix examples: `.github/workflows/build-pr.yaml`, `.github/workflows/build-release.yaml`.

## Project-specific conventions
- Preserve init/fini lifecycle pairing in C code (`*_init` / `*_fini`) and propagate `ChiakiErrorCode` rather than introducing exceptions in `lib/`.
- Respect compile-time feature gates in CMake and source (`CHIAKI_ENABLE_*`, `CHIAKI_GUI_ENABLE_*`, `CHIAKI_LIB_ENABLE_*`).
  - Many options are tri-state (`AUTO|ON|OFF`), not booleans.
- GUI behavior is heavily settings-driven via `Settings` and `QmlSettings`; avoid hardcoding defaults in session code.
- Protobuf is generated at build time from `lib/protobuf/takion.proto`; do not commit generated `takion.pb.c/.h` artifacts.
- Keep platform-specific logic behind existing guards (`Q_OS_*`, `WIN32`, `CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE`, `CHIAKI_GUI_ENABLE_SETSU`, etc.).

## Dependencies and integration points
- Core integrations: Qt6 (Core/Gui/Qml/Quick/Widgets), SDL2, FFmpeg, libplacebo, curl (WebSocket support), json-c, miniupnpc, OpenSSL or mbedTLS, Nanopb, Jerasure, Opus.
- Optional integrations controlled by flags: SpeexDSP, HIDAPI + FFTW (Steam Deck native), Setsu (`udev` + `evdev`), Raspberry Pi decoder.
- PSN remote-connect path crosses GUI and lib:
  - GUI token/connection flow in `gui/src/qmlbackend.cpp`.
  - Holepunch/RUDP primitives in `lib/src/remote/` and `lib/src/session.c`.

## Editing boundaries
- Prefer editing first-party code under `lib/`, `gui/`, `cli/`, `test/`, `cmake/`, `scripts/`.
- Avoid broad edits in vendored trees unless explicitly required: `third-party/`, `ffmpeg/`, `SDL2-*`, `sdl2-compat-*`, `switch/borealis/`.
- If you change build options or dependencies, update the relevant workflow/script/docs file in the same PR.