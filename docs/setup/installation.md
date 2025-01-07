# Installing `chiaki-ng`

=== "Linux Flatpak"

    !!! Tip "Copying from and Pasting into Konsole Windows"

        You can copy from and paste into `konsole` windows with ++ctrl+shift+c++ (copy) and ++ctrl+shift+v++ (paste) instead of the normal ++ctrl+c++ (copy) and ++ctrl+v++ (paste) shortcuts. In fact, ++ctrl+c++ is a shortcut to terminate the current process in the `konsole`. Additionally, you can still right-click and select copy or paste as per normal.

    === "Using the Discover Store (Recommended)"

        1. Open the Discover store

            ![Open Discover](images/OpenDiscover.png)

        2. Search for `chiaki-ng` in the search bar

            ![Install chiaki-ng](images/InstallChiaking.png)

        3. Click Install

    === "Using the `konsole` (If for some reason it doesn't show up on the Discover store)"

        1. Run the following command in the `konsole`

            ```
            flatpak install -y io.github.streetpea.Chiaki4deck
            ```

    !!! Note "About `chiaki-ng`"

        The above instructions are for the official `chiaki-ng` flatpak on Flathub.
        
        However, you can also build the flatpak yourself (recommended for users who want to add their own source code changes on top of the ones I've made) by following the instructions in [Building the flatpak yourself](../diy/buildit.md){target="_blank" rel="noopener"}.

=== "MacOS via Brew"

    1. Install with the following command in the terminal

        ```
        brew install --cask streetpea/streetpea/chiaki-ng
        ```
    
    > [!NOTE]
    > By using chiaki-ng, you acknowledge that it's not [notarized](https://developer.apple.com/documentation/security/notarizing_macos_software_before_distribution).
    >
    > Notarization is a "security" feature by Apple.
    > You send binaries to Apple, and they either approve them or not.
    > In reality, notarization is about building binaries the way Apple likes it.
    >
    > [Homebrew installation script](https://github.com/nikitabobko/homebrew-tap/blob/main/scripts/chiaki-ng.rb) is configured to
    > automatically delete `com.apple.quarantine` attribute, that's why the app should work out of the box, without any warnings that
    > "Apple cannot check chiaki-ng for malicious software"

=== "MacOS/Windows/Linux Appimage Package"

    1. Download the appopriate package from the [releases page](https://github.com/streetpea/chiaki-ng/releases){target="_blank" rel="noopener"} on GitHub (for Mac there are separate packages for the Intel (`-amd64`) and Apple (`-arm64`) based Macs)