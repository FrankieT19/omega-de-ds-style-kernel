# DS Style Kernel for EZ-FLASH OMEGA Definitive Edition

DS Style is a custom kernel for the **EZ-FLASH OMEGA Definitive Edition**. It is based on the official EZ-FLASH OMEGA DE kernel source and replaces the stock launcher with a Nintendo DS-inspired interface, theme system, thumbnail views, language options, and related quality-of-life changes.

> [!WARNING]
> This repository is for the **EZ-FLASH OMEGA Definitive Edition only**. Do not flash this build to the original EZ-FLASH OMEGA.

## Features

- DS-style launcher interface
- Multiple colour themes and light/dark presentation support
- Customisable launcher assets through DS Style Customiser
- List, horizontal carousel, and vertical carousel file views
- Title and box-art thumbnail modes
- Favourite and recent game support
- Multi-language launcher text
- Safer UI sound handling
- Runtime settings stored in readable `SYSTEM/SETTINGS.TXT`

## Building

Install devkitPro/devkitARM, then run:

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
- DS Style custom kernel by FrankieT19
- FatFs by ChaN, as used by the upstream kernel

## License

This project follows the license terms provided with the original EZ-FLASH kernel source. See `LICENSE`.
