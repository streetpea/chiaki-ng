# PlayStation Remote Play on Steam Deck

!!! Abstract "Site Purpose"

    This site serves as a guide to enable you to have the best experience with PlayStation Remote Play on the Steam Deck. Currently, it employs an updated Chiaki package with Steam Deck enhancements I have dubbed `chiaki4deck`. I have created the accompanying `chiaki4deck` flatpak as a means to provide [updates to Chiaki for Steam Deck users](updates/index.md){target="_blank" rel="noopener"}. I am contributing the updates upstream to the main Chiaki project with the hope that they are :fingers_crossed: added despite it currently being in maintenance mode.

## Getting Started

Start by visiting the [Setup section](setup/index.md){target="_blank" rel="noopener"} and following each of the subsections to learn how to setup Chiaki for the best experience on your Steam Deck using `chiaki4deck`.

## Getting Updates

Visit [chiaki4deck Releases](updates/releases.md){target="_blank" rel="noopener"} for instructions on updating to the newest release, with notes for each release.

## Additional Information

### About Chiaki

[Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki){target="_blank" rel="noopener"} is a "Free and Open Source PlayStation Remote Play Client" licensed under the [GNU Affero General Public License version 3](https://www.gnu.org/licenses/agpl-3.0.html){target="_blank" rel="noopener"} (AGPL V3). This license enables anyone to share and modify the source code to add additional features on the condition that they make those publicly available (copy-left) and also license them under the same AGPL V3 license. This package currently has an official flatpak on flathub which you can install on the Steam Deck.

### About `chiaki4deck`

`chiaki4deck` aims to provide tips and tricks on using Chiaki with the Steam Deck. In the spirit of Chiaki being open source, I have made changes to the Chiaki project to optimize the experience on my Steam Deck and am sharing how to access and use those enhancements on this site. Furthermore, I have submitted a patch to bring the first set of completed updates to the "official" Chiaki repo. Additionally, I plan to submit future updates as patches and hope :fingers_crossed: they will be added despite the Chiaki project being in maintenance mode. If they are added, all of the `chiaki4deck` documentation here will also apply to the official Chiaki flatpak. 

!!! Question "What is the `chiaki4deck` flatpak?"

    I have provided a flatpak with my updates named `chiaki4deck` (to distinguish it from the official Chiaki release to avoid confusion if users have both installed) as a solution for users that want an easy way to use these improvements on their Steam Deck. For installation instructions, see the [Installation section](setup/installation.md){target="_blank" rel="noopener"}
    
For users that want to build from source, I have also provided the updated source code on the accompanying GitHub repo (the link to the GitHub is on the top right [top left menu on mobile] of the site banner you see if you scroll to the top of any page). Instructions for this are in the [DIY section](diy/buildit.md){target="_blank" rel="noopener"}. As it stands, a lot of the documentation applies to the general Chiaki flatpak in addition to the `chiaki4deck` flatpak, but instructions are specifically tailored to `chiaki4deck` and the updates included therein. To use this documentation with the Chiaki flatpak replace `io.github.streetpea.Chiaki4deck` with `re.chiaki.Chiaki` where applicable and note that the features listed in the [Updates section](updates/done.md){target="_blank" rel="noopener"} will not work with that version (they only work with `chiaki4deck` until the main `Chiaki` flatpak gets an update.)

## Submitting Documentation Updates

If you want to update the documentation to add helpful information of your own, you can scroll to the top of the page you want to edit and click the paper with pencil icon on the top right (to the right of the current page's title). This will enable you to make edits and submit them for approval. If you have more detailed edits or a new contribution, you can build the documentation locally and see the changes rendered as you make and save them by following the [Building the Documentation Yourself section](diy/builddocs.md){target="_blank" rel="noopener"}.

## Making Suggestions for Improvements to the Documentation

Please submit general issues to the [chiaki4deck GitHub](https://github.com/streetpea/chiaki4deck/issues){target="_blank" rel="noopener"} as well as specific issues related to a given page by clicking the :material-heart-broken: underneath "Was this page helpful?" and opening the feedback form for the page you think needs updating.

## Acknowledgements

* Thanks to the following individuals:
    - Chiaki Authors (including but not limited to Florian Märkl)
    - Reddit users and others who have helped me in my personal Chiaki journey such as u/mintcu7000 with his getting started guide on Reddit
    - Egoistically for the RGB update patch
    - Florian Grill for his gracious help with reverse engineering the PlayStation remote play protocols
    - SageLevi and Superwormy for creating Chiaki4deck artwork

* Thanks to the following open-source projects for inspiration around Steam Deck gyro and haptics:
    - [Steam Controller Singer](https://github.com/Roboron3042/SteamControllerSinger){target="_blank" rel="noopener"}
    - [Steam Deck Gyro DSU](https://github.com/kmicki/SteamDeckGyroDSU){target="_blank" rel="noopener"}

## Maintainer

* [Street Pea](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener"}

## Contributors

* [Johannes Baiter](https://github.com/jbaiter){target="_blank" rel="noopener"}
* [Jamie Bartlett](https://github.com/Nikorag?tab=repositories){target="_blank" rel="noopener"}
* [Joni Bimbashi](https://github.com/jonibim){target="_blank" rel="noopener"}
* [David Rosca](https://github.com/nowrep){target="_blank" rel="noopener"}
* [Street Pea](https://github.com/streetpea){target="_blank" rel="noopener"}