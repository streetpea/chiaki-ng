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

### 1.1.0

#### What You Need to Do to Update

1. [Updating `chiaki4deck`](#updating-chiaki4deck)

2. Revisiting (running back through) the [Automation section](../setup/automation.md){target="_blank" rel="noopener noreferrer"} (don't need to revisit any of the other sections). This is really quick if you use the "Automated Instructions (Recommended)" Tab. 

    !!! Success
        Once your script is updated, since it will be in the same location as before, the Game Mode and controller setup will automatically carry over to this updated automation script. Thus, after revisiting the [Automation section](../setup/automation.md){target="_blank" rel="noopener noreferrer"}, you can immediately go back to using `chiaki4deck`.

3. *[If desired]* Visit the [Using a DualSense and/or DualShock4 Controller with `chiaki4deck`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki4deck){target="_blank" rel="noopener noreferrer"} to see how to setup native touchpad and gyro controls when playing `chiaki4deck` with the DualShock4 and DualSense controllers.

`chiaki4deck`'s 1st update since initial release including the following changes:

#### What you Get by Updating

1. PlayStation controller native touchpad + gyro controller support enabled for the flatpak with setup instructions in [Using a DualSense and/or DualShock4 Controller with `chiaki4deck`](../setup/controlling.md#using-a-dualsense-andor-dualshock4-controller-with-chiaki4deck){target="_blank" rel="noopener noreferrer"}. This is great for when your using a TV or monitor with your Steam Deck.

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