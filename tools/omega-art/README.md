# Omega art tools: box art for emulated games beyond the CUSTOM limit

These are optional helper scripts for the PC. They give **GB/GBC, SMS/GG/SG and PC Engine** games, plus GBA ROM hacks that share a retail code, real box art in DS Style **without using CUSTOM slots**. The kernel itself does not change.

> No ROMs, emulators, artwork or saves are included. Users supply their own dumps, and download the emulators from their authors.

## Why
DS Style's exact-name `SYSTEM/IMGS/CUSTOM/<stem>.bmp` lookup is great, but `LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX` is 256 per folder. A full GB/GBC library goes far beyond that.

## How
The game-code lookup (`SYSTEM/IMGS/<c0>/<c1>/<CODE>.bmp`, plus `IMGS2` for box mode) has no such limit, but it only runs for `.gba` files. So each game becomes its own standalone `.gba`:

- **GB/GBC:** Jagoombacolor + ROM. This is the same image the kernel builds in PSRAM.
- **SMS/GG/SG:** SMSAdvance + its 64-byte header + ROM.
- **PCE:** PCEAdvance + its header + ROM.

Then the scripts:
1. write a **unique code** at 0xAC (`G???`, `S???` or `T???`, base-36 from the SHA-1 of the filename). The code never collides with existing `IMGS` codes, other `.gba` files, or `source/saveMODE.h`.
2. fix the **header complement at 0xBD**.
3. write the thumbnail in the kernel's format: 120×80, BGR555, top-down, **19,256 bytes**.

Your original ROMs are never modified.

## Usage
Requires Python 3 and Pillow.
```
python3 omega_art.py ROMS/ COVERS/ --out OUT --emu jagoombacolor.gba     # GBA hacks + GB/GBC
python3 omega_emu_bundle.py ROMS/ ART/ --out OUT --smsadvance smsadvance.gba --pceadvance pceadvance.gba \
    --imgs SD/SYSTEM/IMGS --gba-roms SD/ --savetable ../../source/saveMODE.h [--custom]
```
Copy `OUT/roms/*.gba` and merge `OUT/SYSTEM/IMGS*` onto the SD card. `libretro_match.py` shows how filenames are matched to libretro-thumbnails `Named_Boxarts`, using exact, core-title and fuzzy matching. `make_pce_testrom.py` generates a tiny CC0 PCE test ROM, so the pipeline can be tested without any commercial ROM.

## Limits and saves
- Bundled games use the GBA save path, so existing GB-path `.sav` files are not picked up automatically. In-game saves are more reliable than emulator savestates. Test one game first.
- Each bundle carries a copy of the emulator, about 75–200 KB.
- Emulator compatibility is unchanged.

**Tested on real hardware:** Omega DE with DS Style. Art displays for bundled GB/GBC, GG/SMS and PCE games.

## Credits
FrankieT19 (DS Style) · FluBBa (Jagoombacolor/Goomba Color, SMSAdvance, PCEAdvance) · EZ-Flash team (kernel) · Sterophonick (SimpleLight) · libretro-thumbnails · No-Intro.
