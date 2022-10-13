# `chiaki4deck` Releases

## How to Prevent Desktop Mode Bug From Occurring

!!! Warning "Steam Deck Bug Discovered that Affects Official Chiaki and by Inheritance (`chiaki4deck`)"

    !!! Bug "Steam Deck Speaker Fails to Load, Crashing Official Chiaki and `chiaki4deck` in Desktop Mode"

        In Desktop Mode, the Steam Deck speaker (Raven) can fail to load on startup. This is rare but seems to happen every once in a while. Unfortunately, this reliably crashes Chiaki and by inheritance `chiaki4deck`, causing it to hang indefinitely. The only ways to exit Chiaki are to close the `konsole` window that launched it (if launched via automation), forcibly kill the process (`kill -9`) or forcibly shutoff the Steam Deck. I am working on a way to prevent this from crashing Chiaki in a bugfix (and hopefully :fingers_crossed: someone is working on the underlying Steam Deck speaker driver issue causing this). In the meantime, **there are 2 good workarounds that prevent this from occurring** (either one works). Please use one of the following when using Chiaki or `chiaki4deck`:
        
        - After booting into Desktop Mode, adjust the volume of the Steam Deck (hit the + or -) volume button (only needs to be done once per boot). You will see this cause Raven to load if it hasn't already. Now, you are safe to launch either Chiaki or `chiaki4deck` at any time until you turn off your Steam Deck.

            **OR**

        - Use `chiaki4deck` and/or `Chiaki` in Game Mode when possible; the speaker not loading hardware error doesn't occur there.

## Updating `chiaki4deck`

In order to update your already installed `chiaki4deck` to the newest version, either:

- Check for updates in `Discover` and update there

    **OR**

- Update via the `konsole` with:

    ``` bash
    flatpak update --user -y re.chiaki.Chiaki4deck
    ```

## Releases (Newest First)

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