# Set Up Remote Play

This section covers how to set up PlayStation Remote Play (using `chiaki-ng`) on your client device [i.e., Steam Deck]. It covers installing the `chiaki-ng` package, configuring an automatic (scripted) launch, and adding `chiaki-ng` to _Game Mode_ with icons and a custom controller configuration to boot. Please switch into _Desktop Mode_ for this process.

!!! Tip "Open This Documentation on Your client device [i.e., Steam Deck] for Easy Copy/Pasting"

    If you open this documentation in a web browser on your client device [i.e., Steam Deck] (such as using the Firefox flatpak), you can easily copy and paste commands by clicking the copy icon to the right of each command (appears when hovering over the right end of a code box):

    ![Copy](images/CopyExample.png)

    and pasting with ++ctrl+v++ or right-click->paste in regular windows and ++ctrl+shift+v++ or right-click->paste in `konsole` windows.

???- example "Enabling Keyboard Navigation on MacOS"

    1. Open `System Preferences` and go to the keyboard Settings

    2. Enable Keyboard Navigation

        ![Enable Keyboard Navigation MacOS](images/EnableMacOSKeyNavigation.jpg)

    ???+ Tip "MacOS Versions Prior to Sonoma"

        For MacOS versions prior to Sonoma, you will have to go to the shortcuts tab to enable keyboard navigation.

        ![Enable Keyboard Navigation Older MacOS](images/EnableMacOSKeyboardNavigationOlder.png)

???+ example "Switching into _Desktop Mode_ on Steam Deck"

    1. Hit the `Steam` button on your Steam Deck

    2. Navigate to the `Power` section on the menu

        ![Steam Menu Power](images/SteamPower.jpg)

    3. Choose `Switch to Desktop`

        ![Switch to Desktop](images/SwitchToDesktop.jpg)

    !!! Tip "Navigating Desktop Mode"

        This install requires entering into _Desktop Mode_. Please connect the following to your Steam Deck:
        
        === "Highly Recommended"

            - external keyboard (You can use `Steam` + ++x++ to bring up and use the virtual keyboard and make liberal use of copy/paste if you really need to, but it is a much worse experience)

        === "Optional but Recommended if you Have Available"
        
            - external mouse (can use the touchpad/trackpads on the device instead) 
            
            - external monitor (can use the native Steam Deck screen instead)