# Chiaki for Android (NG)

> [!WARNING]
> This fork has been **completely vibecoded** with the assistance of AI. Use at your own risk. I am absolutely not an expert in video processing stuff.

Chiaki is a free and open-source PlayStation Remote Play client for Android. This is a specialized fork focused on providing the best possible experience on Android devices, with enhanced features and performance.

My main issue with all of the android remote play apps is that they do not support any debanding filters, which is a dealbreaker for me. Only time when gradients are smooth is when HDR is enabled but this is not a good solution for most of android devices due to reduced brightness and some other weird behaviors. So I thought why not spend some time on implementing debanding filters myself and learn more about AI Agent-driven development.
Basically debanding is already implemented in chiaki-ng for Steam Deck and Windows through libplacebo, but libplacebo is really really not easy to implement for Android, so I decided to try another lightweight approach which is based on GLES Custom Shader. So I spent few evenings to debug and optimize the shader code and finally I got it working! It means now you can play your favorite PS games with smooth gradients and great brightness on your android device!

## Key Features

- **PS4 & PS5 Support**: Connect to your PlayStation 4 or PlayStation 5 from anywhere.
- **Enhanced Video Quality**:
  - **Dynamic Bitrate**: Manually set your bitrate up to **100,000 kbps** for ultra-clear streaming.
  - **Post-processing Filters**: Integrated specialized **Debanding** to reduce color artifacts.
- **Advanced Controls**:
  - **Touchpad Emulation**: Use your touchscreen as a DualShock 4 / DualSense touchpad.
  - **Interactive Button Remapping**: Easily remap any button on your physical controller by pressing the button you want to assign.
  - **Motion Sensors**: Use your Android device's gyroscope and accelerometer for motion-controlled games.
  - **Haptic Feedback**: Support for rumble and touch haptics.
- **Performance Optimized**: Low-latency streaming optimized for Android NDK.

## Getting Started

### Prerequisites

- A PlayStation 4 or PlayStation 5 console connected to your network.
- Your PSN Account ID (you can find it using the provided script).

### Installation

1. Download the latest APK from the [Releases] page.
2. Install the APK on your Android device.
3. Open Chiaki and follow the instructions to register your console.

### Obtaining your PSN Account ID

To register your console, you need your 8-byte PSN Account ID (Base64). You can use the helper script provided in this repository:

```bash
python3 scripts/psn-account-id.py
```

## Build Instructions (Developers)

To build the project yourself:

1. Clone the repository with submodules:
   ```bash
   git clone --recursive https://github.com/SalamiTheMan/chiaki-ng-android-extended.git
   ```
2. Open the `android/` directory in Android Studio.
3. Ensure you have the Android NDK and CMake installed.
4. Build the project using Gradle.

## Upcoming Features (TBD)

- **AMD FidelityFX FSR 1.0**: High-quality upscaling and sharpening (logic integrated into the renderer, UI toggle coming soon).
- **Customizable Overlays**: More layouts for on-screen controls.

## Disclaimer

This project is not endorsed or certified by Sony Interactive Entertainment LLC. PlayStation, DualShock, and DualSense are trademarks of Sony Interactive Entertainment LLC.
