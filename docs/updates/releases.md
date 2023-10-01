# `chiaki4deck` Releases

## Updating `chiaki4deck`

In order to update your already installed `chiaki4deck` to the newest version, either:

- Check for updates in `Discover` and update there

    **OR**

- Update via the `konsole` with:

    ``` bash
    flatpak update --user -y re.chiaki.Chiaki4deck
    ```

## Releases (Newest First)

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