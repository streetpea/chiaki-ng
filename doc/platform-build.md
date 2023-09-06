
# Platform-specific build instructions

## Fedora

On Fedora, build dependencies can be installed via:

```
sudo dnf install cmake make qt5-qtmultimedia-devel qt5-qtsvg-devel qt5-qtbase-gui ffmpeg-devel opus-devel openssl-devel python3-protobuf protobuf-c protobuf-devel qt5-rpm-macros SDL2-devel libevdev-devel systemd-devel speexdsp
```

Then, Chiaki builds just like any other CMake project:
```
git submodule update --init
mkdir build && cd build
cmake ..
make
```

In order to utilize hardware decoding, necessary VA-API component needs to be installed separately depending on your GPU. For example on Fedora:

* **Intel**: `libva-intel-driver`(majority laptop and desktop) OR `libva-intel-hybrid-driver`(most netbook with Atom processor)
* **AMD**: Already part of default installation
* **Nvidia**: `libva-vdpau-driver`

## Windows

Windows support is reduced to the absolute minimum for maintainability.
Official Windows builds are done on AppVeyor within MSYS2 using this script, which can also work as a template for building locally: [scripts/appveyor.sh](../scripts/appveyor.sh).
Other methods of building may work, but will not be officially supported.
