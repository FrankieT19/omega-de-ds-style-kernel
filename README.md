# DS Style Kernel for EZ-FLASH OMEGA Definitive Edition

DS Style is a custom kernel for the **EZ-FLASH OMEGA Definitive Edition**. It is based on the official EZ-FLASH OMEGA DE kernel source and Sterophonick's SimpleLight custom kernel and replaces the stock launcher with a Nintendo DS-inspired interface, theme system, thumbnail views, language options, and related quality-of-life changes.

> [!WARNING]
> This repository is for the **EZ-FLASH OMEGA Definitive Edition only**. Do not flash this build to the original EZ-FLASH OMEGA.

## Current Release

DS Style v8.1. Copy the contents of the download's `SD Card` folder to the root of your microSD card, then boot while holding **R**. Artwork is included; no separate optional-artwork step is needed. Back up your card first. Existing games, saves and settings remain compatible.

Supports the original Omega DE **Rev.A and Rev.B**, not the newer **Omega Definitive Edition B (EZODE-B)**.

## User Guide

Read the complete [DS Style User Guide](https://frankiet19.github.io/omega-de-ds-style-kernel/) for installation, everyday use, artwork, customisation and troubleshooting.

## Web

- [DS Style homepage](https://frankiet19.github.io/omega-de-ds-style-kernel/project/)
- [DS Style Manager](https://frankiet19.github.io/omega-de-ds-style-kernel/manager/) - guided installation, preferences, artwork and styles

## Features

- Start + Select opens the redesigned in-game add-on menu; hold Select to return to the launcher.

- DS-style launcher interface
- Multiple accent colours and Light, Dark or clock-based Auto theme
- Customisable launcher assets through DS Style Customiser
- List, List + Art, horizontal carousel, and vertical carousel file views
- Title and box-art thumbnail modes
- Favourite and recent game support
- Multi-language launcher text, including Chinese and Thai
- Bundled text and Markdown viewer with light and dark modes
- Included title, box and console-folder artwork in the standard installation
- UI sounds
- Smooth carousel animations enabled by default, adjustable side-preview sizes and a launch pulse
- Wrap-around browsing and menus enabled by default, with defensive file checks
- Runtime settings stored in readable `SYSTEM/SETTINGS.TXT`

## Building

Install devkitPro/devkitARM and Python 3 (available as `python` on PATH), then run:

```bat
build.bat
```

The build script outputs:

```text
ezkernelnew.bin
```

Copy `ezkernelnew.bin` to the root of the SD card and boot the cartridge while holding **R** to update the kernel.

## Skin Assets

Theme image headers are generated from the files in `images/` using:

```bat
Grit\Build Skin Files.bat
```

Run this before building if you edit the theme BMP/PNG/JPG assets.

## Customising

For normal users, the recommended route is **DS Style Customiser**, which creates a private project copy, edits the assets/settings, and builds the kernel without modifying this source tree directly.

## Credits

- Original kernel source by EZ-FLASH
- SimpleLight custom kernel by Sterophonick
- DS Style custom kernel by FrankieT19
- Contributions by Merkin
- Contributions by timdmackey
- Thai language support by aidiadayo
- Case-sensitive `HZK12.h` include fix by Kekun
- FatFs by ChaN, as used by the upstream kernel

The kernel uses the included GB/GBC emulator header. The [emulator source and build script](https://github.com/FrankieT19/omega-de-ds-style-kernel/releases/download/v8.1/DS-Style-v8.1-GB-GBC-Emulator-Source.zip) are available separately.

## License

See [LICENSE](LICENSE).
