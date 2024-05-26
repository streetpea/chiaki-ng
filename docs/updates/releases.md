# `chiaki4deck` Releases
## Moving to FlatHub

!!! Caution "Legacy Repo Deprecation"

    `Chiaki4deck` is now on flathub. If you previously installed `chiaki4deck` via the konsole the flatpak has changed to `io.github.streetpea.Chiaki4deck`. The old repo is now **deprecated** in favor of using Flathub since Flathub is added by default to the Steam Deck and the hosting is at no cost to the project (unlike the initial repo). While the software will still be accessible from the old repo, it is encouraged for users to switch over.

To migrate to the flathub repo do the following:

1. Move chiaki4deck's configuration files to their new location

    ``` bash
    mv ~/.var/app/re.chiaki.Chiaki4deck ~/.var/app/io.github.streetpea.Chiaki4deck
    ```

2. Update automatic launcher script to use `io.github.streetpea.Chiaki4deck`

    ``` bash
    sed -i 's/re.chiaki.Chiaki4deck/io.github.streetpea.Chiaki4deck/g' ~/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh 
    ```

3. Uninstall the existing legacy `chiaki4deck` flatpak 

    ``` bash
    flatpak uninstall -y re.chiaki.Chiaki4deck
    ```

4. Install chiaki4deck from flathub using Discover store app or

    ``` bash
    flatpak install -y --user flathub io.github.streetpea.Chiaki4deck
    ```

5. Change properties of non-steam game to point to:

    ``` bash
    /home/deck/.var/app/io.github.streetpea.Chiaki4deck/config/Chiaki/Chiaki-launcher.sh
    ```

## Updating `chiaki4deck`

In order to update your already installed `chiaki4deck` to the newest version, either:

- Check for updates in `Discover` and update there

    **OR**

- Update via the `konsole` with:

    === "Chiaki4deck Installed Via Discover Store"

        ``` bash
        flatpak update -y io.github.streetpea.Chiaki4deck
        ```

    === "Chiaki4deck Legacy Install Via Konsole"

        ``` bash
        flatpak update --user -y re.chiaki.Chiaki4deck
        ```

## Releases (Newest First)

### 1.7.1

Improves remote connection via PSN + adds PS4 support

- remote connection via PSN now supports PS4 consoles
- remote connection via PSN now supports more network types

### 1.7.0

Enables remote connection via PSN

- Remote connection via PSN now available without the need for port forwarding ([see remote connection docs for more details](../setup/remoteconnection.md#remote-connection-via-psn){target="_blank" rel="noopener"}) on how to setup and use.
- Allow zooming out from resolution for users targeting a resolution beyond their screen resolution (i.e., 1080p on Steam Deck)
- Enable don't fragment for MacOS Big Sur and later for more accurate MTU readings during Senkusha
- Create a fallback session id when session id isn't received instead of erroring out
- Workaround patch for vulkan ffmpeg hw decoder bug affecting Nvidia 30 series graphics card users
- Add additional option for obtaining the PSN AccountId via logging in with your psn username and password (in addition to the public lookup)

### 1.6.6

Lowers required mtu from 1435 to 576

- Enable using mtu as low as 576
- Enable gyro on Steam Deck automatically (can now set gyro to none in Steam Settings and still use native gyro)
- Adjust haptic rumble to work better with rumble motors
- Add variable zoom (accessible via Stream Menu)
- Enable mic support on MacOS (now on all platforms)
- Fix issues with address when registering manual consoles
- Update steam shortcut creation to cover additional edge cases
- Add option to reset key mappings to default in Settings
- Add Steam shortcut option to Windows build

### 1.6.5

Graduate DualSense features from experimental status

- Provides quiet haptic rumble for Steam Deck (default) in addition to optional noisy haptics
- Adds haptic rumble for MacOS
- Allow escape key to be selected for key mappings in Settings
- Add option to start the stream with the mic unmuted
- Add Steam Shortcut now also available on Windows

### 1.6.4

Enables creating steam shortcut with default controller profile from menu

- Adds create Steam Shortcut option for Linux and Mac
- Allow setting console pin for console in settings
- Add window type option in settings (fullscreen, stretch, zoom)
- Fix issue causing extra blank ip manual console added when registering non-manual console
- Add option to adjust when Wifi connection symbol appears based on % of dropped packets in 200ms interval

### 1.6.3

Small patch release

- Trade-off minor image artifacts for smoother stream
- Fix race condition causing remote play on console has crashed
- Fix issue where dropped packets results in losing mic connection
- Correctly set render preset
- Set Dualsense microphone and LED to match microphone mute status
- Add PS5 Rumble for controllers other than DualSense and Steam Deck (which have native haptics support)
- Enable game mode in MacOS
- Fix congestion control stop on Windows and log Auto audio output when chosen
- Add alternate option of using flipscreen.games to query PSN Login
- Correctly replace old reference frames and update bitstream parsing

### 1.6.2

Small patch release

- Fixes framepacing regression introduced in 1.6.0
- Dualsense haptics now work again on Linux
- Corrects stretch and zoom when using stream command
- Stop stream freezing on close on Windows
- Stop stream crashing on close on Windows
- Report corrupted frames earlier resulting in less frames dropped when a corrupted frame occurs
- Fix mac arm build not opening and reporting as damaged due to not being signed (was also backported to 1.6.1)

### 1.6.1

Small patch release

- Fixes console registration
- Fixes fullscreen double-click and F11 shortcuts
- Don't close main window when closing stream session on MacOS
- Adds option to sleep PlayStation when sleeping Steam Deck

### 1.6.0

Touch friendly and controller navigable GUI

- New touch-friendly and controller navigable GUI
- MacOS support and Windows libplacebo renderer support
- Resume connection from sleep mode on Steam Deck
- Ability to login to PlayStation for account ID via GUI
- Fix Senkusha, lowering time for console connection and properly setting MTU
- Audio Switch to SDL including fixing audio lag building over session
- New Logo
- Qt6 support

### 1.5.1

Small patch release

- Workaround for gamescope bug (in Steam Deck Preview channel) causing HDR surface swapchain creation to hang.

### 1.5.0

HDR support

- Adds HDR support for Chiaki4deck perfect with the Steam Deck OLED ([see the configuration section](../setup/configuration.md#hdr-high-dynamic-range) for more details).
- New libplacebo vulkan renderer with better picture quality due to post-processing techniques like debanding (now the default renderer)
- Adds option to use controller by positional layout instead of button labels (particularly for Nintendo-style controllers)
- Adds launcher script for appimage
- Adds vulkan video decoding for video cards that support it (Steam Deck doesn't)
- Implements basic FEC error concealment to improve streaming experience (white flashses / green blocking)

### 1.4.1

Small patch release

- Adds multiplier to accelerometer values to match acceleration values of lighter DualSense/DualShock 4 controller. Fixes issue in some games where the acceleration value wasn't high enough when moving/shaking the Steam Deck to trigger the in-game action.

### 1.4.0

Mic support

- Adds mic support to chiaki4deck
- Noise suppression and echo cancelling for mic configurable in the chiaki4deck menu

!!! Question "Why doesn't my bluetooth headset mic show up as an available microphone on Steam Deck?"

    The Steam Deck does not currently support microphones over bluetooth by default so you will need to either use a hardwired headset or the Steam Deck microphone unless you change your Steam Deck settings to enable bluetooth headset support (it is currently experimental and turned off by default due to a noticeable drop in audio quality). Thus, it's advised to use either a hardwired headset or the internal microphone. If you really want to use a bluetooth headset and can live with the drop in audio quality see [How to enable bluetooth headset modes on Steam Deck](https://steamdecki.org/Steam_Deck/Wireless/Bluetooth#Enabling_More_Codecs_and_Enabling_Headsets){target="_blank" rel="noopener"}.

#### Update Actions for Existing Users

1. [Updating `chiaki4deck`](#updating-chiaki4deck)

2. [Optional] Switch to the new default control layout `chiaki4deck+ mic` which adds toggle mic mute to `L4`. Alternatively, you can manually add toggle mic mute to a button of your choice by mapping ++ctrl+m++ to that button.

### 1.3.4

Small patch release

- Adds lowpass filter for haptics and tweaks haptic response => reduced noise while using Steam Deck haptics
- Automatic connection option for GUI
- Update automation script to work for PS4 remote connection

### 1.3.3

Small cosmetic release

- Update icons and display name to `chiaki4deck`

### 1.3.2

Small patch release

- disable Steam Deck haptics when external controllers connected
- add vertical orientation option for motion controls
- let analog trigger actions work w/out PlayStation features enabled
- merge update to RGB mapping with HW accelerated graphics from jonibim

### 1.3.1

Small patch release

- Added scrollbar to settings since bottom of page was cut off on Steam Deck
- Fixed gyro mapping regression (causing drift in some games [i.e., Dreams])
- Updated HIDAPI (dependency) to 0.13.1 due to critical bug in release 0.13.0

Install update following [updating `chiaki4deck`](#updating-chiaki4deck).

### 1.3.0

- Native gyro support for Steam Deck
- Haptics support for DualSense (via USB) and Steam Deck [experimental]
- Adaptive triggers with DualSense (via USB or Bluetooth)
- Automation script tweaks (allow using hostname and specifying external IP / hostname in addition to local one)
- Documentation Updates (new DIY sections on building docs + development builds on Steam Deck + document various 1.3.0 features and changes for new and existing users)
- Bug fixes (audio bug causing crash in base Chiaki fixed, mismatch between cli and automation script leading and lagging space handling fixed, etc.)

#### Update Actions for Existing Users

1. [Updating `chiaki4deck`](#updating-chiaki4deck)

2. [Optional] Enable the experimental PlayStation 5 features (enables PlayStation 5 haptics for Steam Deck and DualSense [via USB] and adaptive triggers for DualSense [via USB or bluetooth]).

    1. Check the box shown in the image below in the GUI.

        ![Enable PlayStation 5 Features](images/EnablePlayStation5Features.png)

    2. [If you are using a DualSense] Turn off Steam Input for the DualSense following the "Turning off Steam Input" tab in [this section](../setup/controlling.md#enabling-chiaki4deck-to-work-with-dualsense-dualshock-4){target="_blank" rel="noopener"}.

3. [Optional] Add an external IP/hostname to the automation by revisiting (running back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}. Doing this will make the automation use your external address if you aren't connected to your home wireless network.

    !!! Note

        This is for those that have gone through the process to make a connection outside of their local network to get an external IP/hostname to use.
    
### 1.2.0

`chiaki4deck`'s 2nd update since initial release.

#### What You Need to Do to Update

1. [Updating `chiaki4deck`](#updating-chiaki4deck)

2. Updating your controller config to the new default (`chiaki4deck+`) and/or updating your custom controller layouts to take advantage of native touchscreen / trackpad controls. See the [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section for details. 

    - Using the Default Controller Profile (Recommended Starting Point):
    
        Open the `chiaki4deck` [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section, browsing the `COMMUNITY LAYOUTS` tab for the `chiaki4deck+` config, downloading it, and setting it as your new layout. 
        
        [Approximate Time Estimate: 1 minute]
        
    - Creating a Custom Controller Profile (Great for tinkering, especially for updating the default profile to meet your needs exactly):
    
        Make a custom controller profile using the [Creating Your Own Controller Profile](../setup/controlling.md#creating-your-own-controller-profile){target="_blank" rel="noopener"} section, taking special note of the [Special Button Mappings](../setup/controlling.md#special-button-mappings-you-need-to-assign-these-yourself){target="_blank" rel="noopener"} and [Using Steam Deck Controller Touchscreen in Your Custom Controller Profile](../setup/controlling.md#using-steam-deck-controller-touchscreen-in-your-custom-controller-profile){target="_blank" rel="noopener"}

        [Approximate Time Estimate: 10-20 minutes depending on your experience with Steam Deck Controller Layouts]

3. [OPTIONAL] If you have a PlayStation Login Passcode and want entering it to be automated, please revisit (run back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"} (don't need to revisit any of the other sections). This is really quick if you use the "Automated Instructions (Recommended)" Tab. [Approximate Time Estimate: 5 minutes]

    !!! Success
        Once your script is updated, since it will be in the same location as before, the Game Mode and controller setup will automatically carry over to this updated automation script. Thus, after revisiting the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}, you can immediately go back to using `chiaki4deck`.

#### What you Get by Updating

1. Full mapping for the PlayStation touchpad to the Steam Deck's touchscreen and trackpad (you can use either or switch between them if you so choose). See the [touchscreen](../setup/controlling.md#using-steam-deck-controller-touchscreen-in-your-custom-controller-profile){target="_blank" rel="noopener"} and [trackpad](../setup/controlling.md#default-chiaki4deck-layout-trackpad-mapping){target="_blank" rel="noopener"} mapping subsections of the [Controller Options](../setup/controlling.md#default-controller-profile){target="_blank" rel="noopener"} section for details.

    !!! note "General Touchscreen and Mouse Support"
        
        This update adds general touchscreen and mouse support for the PlayStation touchpad. Thus, it's applicable beyond the Steam Deck. With regard to `chiaki4deck`, the Steam Deck is the focus / inspiration for the update.

2. Updated RGB color mapping used in OpenGL widget to be more color accurate thanks to [Egoistically's](https://github.com/Egoistically){target="_blank" rel="noopener"} Chiaki fork. See [Updated RGB Mapping](done.md#updated-rgb-mapping){target="_blank" rel="noopener"} for details and a before and after comparison in Ghost of Tsushima.

3. Automatic Login Passcode Entry (For Users with a Login Passcode)

    If you have to enter a login passcode each time you turn on your PlayStation console and login, you can now enter it into the automation to login from your personal Steam Deck without the hassle of bringing up the virtual keyboard with `Steam` ++plus+x++ and entering it in each time.

### 1.1.0

`chiaki4deck`'s 1st update since initial release.

#### What You Need to Do to Update

1. [Updating `chiaki4deck`](#updating-chiaki4deck)

2. Revisiting (running back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener"} (don't need to revisit any of the other sections). This is really quick if you use the "Automated Instructions (Recommended)" Tab. 

    !!! Success
        Once your script is updated, since it will be in the same location as before, the Game Mode and controller setup will automatically carry over to this updated automation script. Thus, after revisiting the [Automation section](../setup/automation.md){target="_blank" rel="noopener"}, you can immediately go back to using `chiaki4deck`.

3. *[If desired]* Visit the [Using a DualSense and/or DualShock4 Controller with `chiaki4deck`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki4deck){target="_blank" rel="noopener"} to see how to setup native touchpad and gyro controls when playing `chiaki4deck` with the DualShock4 and DualSense controllers.

#### What you Get by Updating

1. PlayStation controller native touchpad + gyro controller support enabled for the flatpak with setup instructions in [Using a DualSense and/or DualShock4 Controller with `chiaki4deck`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki4deck){target="_blank" rel="noopener"}. This is great for when your using a TV or monitor with your Steam Deck.

2. Enhanced automated launch

    Specifically, I upgraded the discovery cli command to work properly and updated the automation script (and accompanying generator script) to to take advantage of this command instead of ping to handle some cases that failed intermittently before.

    Now, the automation works for edge cases such as:

    - user is remote playing from outside their home wireless network (given they've already done the networking setup for that)

    - console is in the process of going to sleep or coming online when remote play session launched

    - console is currently downloading a large game

    - user doesn't have ping enabled on his/her wireless network

### 1.0.0

`chiaki4deck's` initial release including the following notable updates:

1. 3 view modes for non-standard screen sizes 

2. Quit function ++ctrl+q++

3. Enabled Automated Launch