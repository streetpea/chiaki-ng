# Contributor Guide

This file is a quick orientation for new contributors working on `chiaki-ng`.

## What This Repository Is

`chiaki-ng` is a cross-platform PlayStation Remote Play client. The codebase is split into:

- shared protocol and codec code in [`lib/`](./lib)
- desktop GUI code in [`gui/`](./gui)
- command-line tooling in [`cli/`](./cli)
- platform-specific frontends in [`android/`](./android) and [`switch/`](./switch)
- build, packaging, and release scripts in [`scripts/`](./scripts) and [`.github/workflows/`](./.github/workflows)

Most feature work on Linux/Windows/macOS goes through `lib/` plus `gui/`.

## Mental Model

At a high level:

1. `lib/` talks to the console.
2. `lib/` receives encrypted control, video, audio, and input-related packets.
3. decoders convert those packets into audio frames, video frames, and controller state
4. `gui/` presents video/audio and turns local user input back into network packets

If you are trying to understand a user-visible bug, start by deciding whether it lives in:

- protocol/session logic
- decode/receive timing
- renderer/audio device handling
- QML/UI behavior

## Important Directories

### Shared Core

- [`lib/include/chiaki/`](./lib/include/chiaki): public/shared headers
- [`lib/src/`](./lib/src): protocol, session, send/receive, codec wrappers, networking
- [`lib/protobuf/`](./lib/protobuf): protobuf definitions used by the protocol layer

Files in `lib/src/` are where most protocol or media-path debugging starts:

- `ctrl.c`: control channel setup and session negotiation
- `takion.c`: core Remote Play transport/session handling
- `videoreceiver.c` / `audioreceiver.c`: packet receive and buffering paths
- `ffmpegdecoder.c` / `opusdecoder.c` / `opusencoder.c`: media codec wrappers
- `controller*.c`, `feedbacksender.c`, `audiosender.c`: input/output back to the console

### Desktop GUI

- [`gui/include/`](./gui/include): C++ GUI headers
- [`gui/src/`](./gui/src): Qt/QML integration, renderer, session bridge
- [`gui/src/qml/`](./gui/src/qml): QML views, dialogs, stream UI
- [`gui/res/`](./gui/res): icons, shaders, bundled assets

Key desktop files:

- [`gui/src/qmlmainwindow.cpp`](./gui/src/qmlmainwindow.cpp): main renderer/window integration, libplacebo, swapchain handling
- [`gui/src/qmlbackend.cpp`](./gui/src/qmlbackend.cpp): bridge between QML and the session/runtime backend
- [`gui/src/streamsession.cpp`](./gui/src/streamsession.cpp): live stream session logic, controller/audio/video hooks
- [`gui/src/settings.cpp`](./gui/src/settings.cpp): persistent settings

### Tests

- [`test/`](./test): unit tests
- current unit coverage is relatively small, so a lot of media/rendering changes still need manual validation

### Packaging and CI

- [`scripts/flatpak/`](./scripts/flatpak): Flatpak manifests and patches
- [`scripts/build-*.sh`](./scripts): dependency and packaging helper scripts
- [`.github/workflows/`](./.github/workflows): CI and release automation

If a dependency version changes, check all three places:

- `CMakeLists.txt`
- `scripts/`
- GitHub workflow files

## How The Main Runtime Fits Together

### Video Path

For desktop streaming, the rough path is:

1. network packet received in `lib`
2. video packet buffering / reassembly in [`lib/src/videoreceiver.c`](./lib/src/videoreceiver.c)
3. decode through FFmpeg in [`lib/src/ffmpegdecoder.c`](./lib/src/ffmpegdecoder.c)
4. decoded frame handed to GUI
5. GUI queues and renders it through libplacebo in [`gui/src/qmlmainwindow.cpp`](./gui/src/qmlmainwindow.cpp)
6. Qt Quick overlays/menu content are composited on top

When debugging video issues, separate:

- packet loss / decode errors
- timestamp / pacing problems
- GPU interop problems
- QML overlay/swapchain issues

### Audio Path

The shared receive side is in [`lib/src/audioreceiver.c`](./lib/src/audioreceiver.c), decoding is in [`lib/src/opusdecoder.c`](./lib/src/opusdecoder.c), and desktop device handling lives in [`gui/src/streamsession.cpp`](./gui/src/streamsession.cpp).

Microphone capture and send-back is the reverse path:

- SDL/Qt audio input on desktop
- optional processing / echo cancellation
- Opus encode
- mic packet send through shared code

### Input Path

Controller input is gathered in the GUI/session layer and converted into protocol packets through shared input sender code. If a problem only reproduces on one frontend, start in that platform’s session/UI layer before touching shared protocol code.

## Where To Start For Common Tasks

### UI or Menu Bug

Start in:

- [`gui/src/qml/`](./gui/src/qml)
- then trace into [`gui/src/qmlbackend.cpp`](./gui/src/qmlbackend.cpp) or [`gui/src/qmlmainwindow.cpp`](./gui/src/qmlmainwindow.cpp) if it crosses into C++

### Renderer Bug

Start in:

- [`gui/src/qmlmainwindow.cpp`](./gui/src/qmlmainwindow.cpp)
- libplacebo version/pinning in `CMakeLists.txt`, `scripts/flatpak`, and workflow files

### Audio/Mic Bug

Start in:

- [`gui/src/streamsession.cpp`](./gui/src/streamsession.cpp)
- [`lib/src/audioreceiver.c`](./lib/src/audioreceiver.c)
- [`lib/src/audiosender.c`](./lib/src/audiosender.c)
- [`lib/src/opusdecoder.c`](./lib/src/opusdecoder.c)
- [`lib/src/opusencoder.c`](./lib/src/opusencoder.c)

### Session/Connection Bug

Start in:

- [`lib/src/ctrl.c`](./lib/src/ctrl.c)
- [`lib/src/takion.c`](./lib/src/takion.c)
- receiver/sender files in [`lib/src/`](./lib/src)

## Practical Workflow

For most changes:

1. inspect the relevant shared/runtime path first
2. make the smallest change that fixes the specific failure mode
3. rebuild in the Flatpak dev environment if you are touching GUI/media code
4. run `ctest --test-dir build --output-on-failure`
5. if the change affects streaming, renderer selection, audio devices, or session setup, do a live/manual test too

## Things That Commonly Need Extra Care

- renderer/backend changes: often require matching updates in QML, C++, CMake, Flatpak scripts, and CI pins
- dependency upgrades: must be kept consistent across system requirements and bundled build scripts
- audio/video timing changes: can look correct in unit tests but still regress live playback
- QML `Window` vs `Item` behavior: input/focus handlers often need to live on focusable `Item`s, not top-level windows
- git worktree hygiene: packaging and CI files are easy to miss when changing runtime dependencies

## Good First Reading Order

If you are brand new, this order gives a decent map quickly:

1. [`README.md`](./README.md)
2. [`CMakeLists.txt`](./CMakeLists.txt)
3. [`gui/src/qmlmainwindow.cpp`](./gui/src/qmlmainwindow.cpp)
4. [`gui/src/streamsession.cpp`](./gui/src/streamsession.cpp)
5. [`lib/src/takion.c`](./lib/src/takion.c)
6. [`lib/src/videoreceiver.c`](./lib/src/videoreceiver.c)
7. [`lib/src/audioreceiver.c`](./lib/src/audioreceiver.c)

<!-- created by codex agent -->
