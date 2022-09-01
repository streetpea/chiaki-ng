# Completed (Done) Updates

## 3 view modes for non-standard screen sizes 

It's great to have these options for Steam Deck's non-standard 1280x800 resolution

1. Normal (default) [used for fullscreen launch option]

    Maintains aspect ratio, adds black bars to fill unused parts of screen

    ![Normal](images/Fullscreen.jpg)

    ![Normal in Sekiro](images/Sekiro_Fullscreen.jpg)

2. Zoom using ++ctrl+z++ to toggle

    Maintains aspect ratio, cutting off edges to fill screen

    ![Zoom](images/Zoom.jpg)

    ![Zoom in Sekiro](images/Sekiro_Zoom.jpg)

3. Stretch using ++ctrl+s++ to toggle

    Stretches image (distorting aspect ratio) to fill screen

    ![Stretch](images/Stretch.jpg)

    ![Stretch in Sekiro](images/SekiroStretch.jpg)

## Quit function ++ctrl+q++

Cleanly quits Chiaki, respecting the user's configuration option of either asking to put PlayStation console sleep, putting PlayStation console to sleep without asking, or leaving PlayStation console on. 

!!! Question "What does this do for me?"

    Now, if you hit a back button (or other button) mapped to ++ctrl+q++ on your Steam Deck, your remote play session will shut down cleanly and put your console to sleep automatically if you so choose. This means you no longer have to manually put your console to sleep via a menu or the power button on the PlayStation console itself.

## Enabled Automated Launch 

This skips the need to visit the configuration screen and use the Steam Deck's touchscreen each time. Additionally, only 1 window is open during game streaming instead of 2, eliminating the flashing issue that would occur during accidental window switching by the Steam Deck's `Game Mode`. I have added a helper script to generate a `Chiaki-launcher.sh` script as well as provided complete instructions in the [Automation section](../setup/automation.md){target="_blank" rel="noopener noreferrer"}

