## System Pre-requisites

### All Systems
- GPU or integrated graphics supporting Vulkan 1.2 or higher

### Linux Flatpak
- pipewire

### Linux Appimage
- libva
- pipewire

## Where to Download 

### Linux
- Flatpak Release on Flathub as io.github.streetpea.Chiaki4deck
- Appimage  release (arm64 and x86_64) attached

### MacOS
- Homebrew
  ```
  brew install --cask streetpea/streetpea/chiaki-ng
  ```
-  Attached releases (arm64 and x86_64) attached

### Windows:
- winget
  ```
  winget install --id=StreetPea.chiaki-ng -e
  ```
- chocolatey
  ```
  choco install chiaki-ng
  ```
 - Attached releases (installer and portable)

## Switch:

Please see akira releases here: https://github.com/xlanor/akira/releases

## Additional Notes
Note: Pipewire required for appimage. For Linux, pipewire is required for DualSense haptics.
For flatpak, you can run with `--env=SDL_AUDIODRIVER=pulse` to use pulse instead of pipewire if you really want that (doing this means DualSense haptics won't work)

## Updates

### v1.10.0

This release provides the following improvements:

- improve streaming quality and stability with better queueing, reset handling, congestion control, and streaming metrics
- add an OpenGL renderer backend plus major renderer and libplacebo fixes, adaptive queue depth, and custom upscalers
- add VSync support plus spatial upscaler presets under settings->video->Render
- improve PSN remote play reliability with configurable NAT port guessing and holepunching fixes
- rework audio, microphone, and haptics paths for better recovery and fewer failure cases
- fix macOS ARM rendering, crash-on-exit, and controller input issues, and add iOS/tvOS compilation support
