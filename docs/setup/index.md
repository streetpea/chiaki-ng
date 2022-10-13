# Set Up Remote Play on Steam Deck

This section covers how to set up PlayStation Remote Play (using Chiaki) on Steam Deck. It covers installing the `chiaki4deck` package, configuring an automatic (scripted) launch, and adding `chiaki4deck` to _Game Mode_ with icons and a custom controller configuration to boot. Please switch into _Desktop Mode_ for this process.

!!! Bug "Steam Deck Speaker Fails to Load, Crashing Official Chiaki and `chiaki4deck` in Desktop Mode"

    In Desktop Mode, the Steam Deck speaker (Raven) can fail to load on startup. This is rare but seems to happen every once in a while. Unfortunately, this reliably crashes Chiaki and by inheritance `chiaki4deck`, causing it to hang indefinitely. The only ways to exit Chiaki are to close the `konsole` window that launched it (if launched via automation), forcibly kill the process (`kill -9`) or forcibly shutoff the Steam Deck. I am working on a way to prevent this from crashing Chiaki in a bugfix (and hopefully :fingers_crossed: someone is working on the underlying Steam Deck speaker driver issue causing this). In the meantime, **there are 2 good workarounds that prevent this from occurring** (either one works). Please use one of the following when using Chiaki or `chiaki4deck`:
    
    - After booting into Desktop Mode, adjust the volume of the Steam Deck (hit the + or -) volume button (only needs to be done once per boot). You will see this cause Raven to load if it hasn't already. Now, you are safe to launch either Chiaki or `chiaki4deck` at any time until you turn off your Steam Deck.

        **OR**

    - Use `chiaki4deck` and/or `Chiaki` in Game Mode when possible; the speaker not loading hardware error doesn't occur there.

!!! Tip "Open This Documentation on Your Steam Deck for Easy Copy/Pasting"

    If you open this documentation in a web browser on your Steam Deck (such as using the Firefox flatpak), you can easily copy and paste commands by clicking the copy icon to the right of each command (appears when hovering over the right end of a code box):

    ![Copy](images/CopyExample.png)

    and pasting with ++ctrl+v++ or right-click->paste in regular windows and ++ctrl+shift+v++ or right-click->paste in `konsole` windows.
    
???+ example "Switching into _Desktop Mode_"

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