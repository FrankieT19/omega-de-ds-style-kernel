# Changelog

## DS Style v7.0

- Added a Hide system files setting for keeping kernel, system, and metadata files out of the root file browser.
- Added artwork matching for `.sav` files when a matching ROM is present.
- Added Settings > Load style for installing `.bin` kernels from `SYSTEM/KERNELS`.

This changelog records the complete DS Style project history. Some early releases or development notes apply specifically to the original Omega or Omega Definitive Edition, as stated.

## DS Style v6.9

- Improved launcher stability by reducing RAM pressure in the menu.
- Added a safety limit for cheat entries to match the space available in the in-game patch.
- Refined list-view scrolling, redraw behaviour, and navigation consistency across launcher menus.
- Refined the sound system for safer, more reliable audio playback during menu navigation.
- Added a Hide system files setting for keeping kernel, system, and metadata files out of the root file browser.
- Added artwork matching for `.sav` files when a matching ROM is present.
- Replaced the old `/THEMES` browser flow with a Settings > Load style option using `.bin` files in `SYSTEM/KERNELS`.

## DS Style v6.8

- Fixed translated settings resetting after restarting.
- Improved accented character support for SD card filenames.
- Corrected the language options written to `SETTINGS.TXT`.

## DS Style v6.7

- Added separate Start screen On/Off and Last Played/Favourites settings.
- Select on the Start screen now switches only between Last Played and Favourites.
- Start on the SD Card screen now opens Recently Played, then switches between Recently Played and Favourites.
- L and R also switch between Recently Played and Favourites within these views.

## DS Style v6.6

- Added custom thumbnail support for folders and all files.
- Improved Thumbnail Scraper integration.
- Added cached custom-art indexes to keep normal browsing responsive.
- Added support for both 120 x 80 title thumbnails and 80 x 80 box thumbnails in `CUSTOM` folders.
- Limited each `CUSTOM` folder to 256 images to remain within the GBA's available RAM.

## DS Style v6.5

- Refined menu behaviour and interface design.
- Added an Off option for the Start screen source, allowing startup to open the SD browser directly.
- Prevented B from returning to the Start screen while the Start screen is disabled.
- Improved held-button navigation in game launch menus.

## DS Style v6.4

- Added multiple language options.
- Added Chinese language handling while retaining the DS Style font for Latin text.
- Added customisable launcher text and Start screen layouts through DS Style Customiser.
- Improved file, backup, thumbnail-read, and directory bounds safety.
- Resumed development for the original Omega and restored feature parity where supported.

> **Original Omega development resumed.**

## DS Style v6.3

- Improved the interface on the SD and NOR screens.

## DS Style v6.2

- Added animation to the Start screen.

## DS Style v6.1

- Refined the sound system.
- Added the option to toggle interface sounds.
- Optimised menu behaviour.

## DS Style v6.0

- Completely rebuilt the menu system around a Nintendo DS-inspired interface.
- Added a new Start screen with Last Played and Favourites.
- Added a quick-start hotkey for launching a game immediately during startup.
- Added an 80 x 80 box-art thumbnail option.
- Moved theme and colour selection into the Settings menu.
- Added all Nintendo DS operating-system colour choices.
- Made dark mode compatible with every colour option.
- Added username support.

> **Original Omega development paused after the v5 releases.**

## DS Style v5.4

- Fixed crashes affecting certain original Omega cartridges.
- Prevented junk audio when selecting delete for NOR games that were not the most recently written game.
- Fixed the original Omega screen not refreshing correctly after writing to NOR.
- Added the Purple theme.

## DS Style v5.3

- Fixed a crash affecting certain Omega Definitive Edition Revision A cartridges.
- Corrected the kernel version shown on the original Omega Help screen.
- Added small interface refinements.

## DS Style v5.2

- Fixed GBC and NGPC icons not displaying correctly.

## DS Style v5.1

- Added the Recently Played title to the top bar in list and horizontal views.

## DS Style v5.0

- Replaced the old thumbnail viewer with two new file-browser interfaces.
- Extended thumbnail browsing to Recently Played and NOR.
- Added interface sound effects.
- Added the Remember setting, which returns the file browser to the last played game after startup or reset.
- Added the Boot Mode setting for choosing the default ROM launch behaviour.
- Renamed HardReset to Full Boot, which launches games with the GBA BIOS introduction.
- Added position memory when moving between menus.
- Added bespoke splash screens for the included themes.
- Included general interface improvements and bug fixes.

> **Original Omega development resumed.**

## DS Style v4.0

- Added a new thumbnail viewer using game-title artwork.
- Updated the Yoshi theme.
- Added the Yoshi theme to the original Omega v3.3 build.

> The legacy Yoshi theme is not currently supported by DS Style v6.

> **Original Omega development paused after v3.3.**

## DS Style v3.3

- Moved the `BACKUP` folder into `SYSTEM`.
- Corrected the Yoshi theme name on the Help screen.

## DS Style v3.2

- Corrected inaccurate information in the Help menu.

## DS Style v3.1

- Added bug fixes.
- Added the bonus Yoshi theme.

> The legacy Yoshi theme is not currently supported by DS Style v6.

## DS Style v3.0

- Added the theme swapper.
- Tidied the Delete File menu.

## DS Style v2.2

- Added every colour option to the original Omega.
- Corrected the theme name and author on the Help screen.

## DS Style v2.1

- Added the dark colour option to the original Omega.

## DS Style v2.0

- Redesigned the header icons.

## DS Style v1.1

- Added support for the original EZ-Flash Omega.

## DS Style v1.0

- Initial release.
