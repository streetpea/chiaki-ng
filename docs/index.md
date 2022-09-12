# PlayStation Remote Play on Steam Deck

!!! Abstract "Site Purpose"

    This site serves as a guide to enable you to have the best experience with PlayStation Remote Play on the Steam Deck. Currently, it employs an updated Chiaki package with Steam Deck enhancements I have dubbed `chiaki4deck`. I have created the accompanying `chiaki4deck` flatpak as a means to provide [updates to Chiaki for Steam Deck users](updates/index.md){target="_blank" rel="noopener noreferrer"} before they are :fingers_crossed: added to the main project. Once the changes are merged into the main project and the project maintainers create a new flatpak release, this site will be updated to use the official `Chiaki` flatpak.

## Getting Started

Start by visiting the [Setup section](setup/index.md){target="_blank" rel="noopener noreferrer"} and following each of the subsections to learn how to setup Chiaki for the best experience on your Steam Deck using `chiaki4deck`.

## Getting Updates

Visit [chiaki4deck Releases](updates/releases.md){target="_blank" rel="noopener noreferrer"} for instructions on updating to the newest release, with notes for each release.

## Additional Information

### About Chiaki

[Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki){target="_blank" rel="noopener noreferrer"} is a "Free and Open Source PlayStation Remote Play Client" licensed under the [GNU Affero General Public License version 3](https://www.gnu.org/licenses/agpl-3.0.html){target="_blank" rel="noopener noreferrer"} enabling users to share and modify the source code to add additional features on the condition that they make those publicly available. This package currently has an official flatpak on flathub which you can install on the Steam Deck.

### About `chiaki4deck`

`chiaki4deck` aims to provide tips and tricks on using Chiaki with the Steam Deck. In the spirit of Chiaki being open source, I have made changes to the Chiaki project to optimize the experience on my Steam Deck and am sharing how to access and use those enhancements on this site. Furthermore, I have submitted a patch to bring the completed updates to the "official" Chiaki repo and at that point all of the `chiaki4deck` documentation here will apply to the main repo instead of needing the `chiaki4deck` flatpak. Additionally, I plan to submit any future updates as patches too.

!!! Question "What is the `chiaki4deck` flatpak"

    I have provided a flatpak with my updates named `chiaki4deck` (to distinguish it from the official Chiaki release to avoid confusion if users have both installed) as a solution for users that want an easy way to use these improvements on their Steam Deck. For installation instructions, see the [Installation section](setup/installation.md){target="_blank" rel="noopener noreferrer"}
    
For users that want to build from source, I have also provided the updated source code on the accompanying GitHub repo (the link to the GitHub is on the top right of the site banner you see if you scroll to the top of any page) as well as instructions in the [DIY section](diy/buildit.md){target="_blank" rel="noopener noreferrer"}. As it stands, a lot of the documentation applies to the general Chiaki flatpak in addition to the `chiaki4deck` flatpak, but instructions are specifically tailored to `chiaki4deck` and the updates included therein. To use this documentation with the Chiaki flatpak replace `re.chiaki.Chiaki4deck` with `re.chiaki.Chiaki` where applicable and note that the features listed in the [Updates section](updates/done.md){target="_blank" rel="noopener noreferrer"} will not work with that version (they only work with `chiaki4deck` until the main `Chiaki` flatpak gets an update.)

## Submitting Documentation Updates

If you want to update the documentation to add helpful information of your own, you can scroll to the top of your current page and click the pencil icon on the top right (to the right of the current page's title) to make edits and submit them for approval.

## Making Suggestions for Improvements to the Documentation

Please submit general issues to the [chiaki4deck GitHub](https://github.com/streetpea/chiaki4deck/issues){target="_blank" rel="noopener noreferrer"} as well as specific issues related to a given page by clicking the :material-heart-broken: underneath "Was this page helpful?" and opening the feedback form for the page you think needs updating.

## Acknowledgements

* Thanks to the following individuals:
    - Chiaki Authors (including but not limited to Florian Märkl)
    - Reddit users and others who have helped me in my personal Chiaki journey such as u/mintcu7000 with his getting started guide on Reddit

## Authors

* [Street Pea](https://www.reddit.com/message/compose/?to=Street_Pea_6693){target="_blank" rel="noopener noreferrer"}