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

## Additional Notes
Note: Pipewire required for appimage. For Linux, pipewire is required for DualSense haptics.
For flatpak, you can run with `--env=SDL_AUDIODRIVER=pulse` to use pulse instead of pipewire if you really want that (doing this means DualSense haptics won't work)

## Updates

