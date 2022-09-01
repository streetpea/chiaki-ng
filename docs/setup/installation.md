# Installing `chiaki4deck`

!!! Tip "Copying from and Pasting into Konsole Windows"

    You can copy from and paste into `konsole` windows with ++ctrl+shift+c++ (copy) and ++ctrl+shift+v++ (paste) instead of the normal ++ctrl+c++ (copy) and ++ctrl+v++ (paste) shortcuts. In fact, ++ctrl+c++ is a shortcut to terminate the current process in the `konsole`. Additionally, you can still right-click and select copy or paste as per normal.

1. Open a `konsole` window (Go to applications in the bottom left corner and type in `konsole` in the search bar to find it)

2. Run the following command in the `konsole` window to install `chiaki4deck` as a sandboxed user-level installation (more secure vs default of system-level installation)

    ``` bash
    flatpak install --user -y https://github.com/streetpea/chiaki4deck/releases/download/v1.0.0/re.chiaki.Chiaki4deck.flatpakref
    ```

    ???+ example "Example Output"

        ``` bash
            re.chiaki.Chiaki4deck permissions:
                ipc                 network               pulseaudio     wayland
                x11                 devices               bluetooth      file access [1]
                dbus access [2]     bus ownership [3]

                [1] xdg-config/kdeglobals:ro
                [2] com.canonical.AppMenu.Registrar, org.freedesktop.ScreenSaver,
                    org.kde.KGlobalSettings, org.kde.kconfig.notify
                [3] org.kde.*


                    ID                    Branch      Op Remote              Download
            1. [✓] re.chiaki.Chiaki4deck chiaki4deck i  chiaki4deck2-origin 101.6 MB / 120.0 MB

            Installation complete.
        ```

!!! Note "About `chiaki4deck`"

    This is a flatpak I created for `chiaki4deck` from [updated](../updates/done.md){target="_blank" rel="noopener noreferrer"} Chiaki source code and signed with the gpg key I have added to the companion GitHub for this site. This is the recommended install as it is the most straightforward.
    
    However, you can also build the flatpak yourself (recommended for users who want to add their own source code changes on top of the ones I've made) by following the instructions in [Building the flatpak yourself](../diy/buildit.md){target="_blank" rel="noopener noreferrer"}.


