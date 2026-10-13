# SMS/GG/PCE box art on EZ-Flash Omega DE + DS Style v7.4 (2026-10-12, box-only prototype)

## 1. Does DS Style CUSTOM name matching work for .sms/.gg/.pce? **Yes. The source confirms it.**
Source: github.com/FrankieT19/omega-de-ds-style-kernel (v7.4, cloned to omega-art/ds-de), `source/ezkernelnew.c`.
- `Launcher_LoadThumbDataForIndex` (~L7375) runs the CUSTOM lookup on **every** SD file, with no extension gate. The game-code (0xAC) lookup only runs for .gba/.agb. Plugin-launched .sms/.gg/.sg/.pce files therefore get art from `SYSTEM/IMGS/CUSTOM/<stem>.bmp` (and `IMGS2/CUSTOM` in Box mode).
- The User Guide p.12 says the same thing: "CUSTOM folders provide artwork for ... emulated games". Matching uses the filename without its extension and is case-insensitive.
- Limits, from the code:
  - The index (`LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX 256`) holds the first 256 files in directory order. Files past 256 are silently ignored.
  - IMGS/CUSTOM and IMGS2/CUSTOM each have their own 256 limit. They do not add up, because only the active style's folder is used.
  - `Sonic.sms` and `Sonic.gg` share one image.
  - The start-screen and favourites preview (`Launcher_StartPreviewEnsureCached`, `Launcher_IsGbaFilename`) only draws art for .gba/.agb. Plugin files show their file icon there.
  - The art BMP format is the same as GBA art (19,256 B, 16-bit). omega_art.make_bmp output works.
- Stock EZ kernel: no plugin support for SMS/PCE at all, and thumbnails are .gba-only.

## 2. Bundle approach (prototyped): emulator + 1 ROM -> .gba with its own code
- Emulators: FluBBa's official ndsretro.com timed out from the box. I took his own official download files through Wayback, unmodified:
  - SMSAdvance **v2.5** (2009-01-26, latest), `smsadvance.gba`, sha1 59bc76d9…
  - PCEAdvance **v7.5** (2005-10-14, latest FluBBa build), `pceadvance.gba`, sha1 0eec2651…
  - patters' v7.5_EZ4 fork only adds EZ-Flash IV PSRAM unlock. It is not used here.
  - Files are in omega-art/smspce/{sms,pce,dl}.
- Formats: SMSAdvance `Formats.txt` (64-byte "SMS",1A header). PCEAdvance per patters' `pceadvance_compile.py` (`<31sc5I12s`, EMUID "NES",1A, size+16).
- Test ROMs (no commercial ROMs):
  - `SMS VDP Test.sms`: FluBBa's freeware SMSVDTST, from his site.
  - `PCE Stripe Test.pce`: a 32 KB ROM I wrote myself (tool/make_pce_testrom.py, CC0). It sets up the VDC/VCE and keeps animating the palette.
- Codes: `S`+3 for SMS/GG/SG and `T`+3 for PCE, base36 from SHA-1 of the title. The DS Style retail save table (2,815 codes) contains no S??? codes and only TVAP among T???.
  - The prototype produced **SRRR** (SMS) and **TI7K** (PCE).
  - The header at 0xBD was recomputed and verified for both (0x23, 0x68).
  - The stock emulators all share SMSA/PCEA, which is why each bundle needs its own code.
- mGBA 0.10.5 headless (Xvfb), screenshots at 15 s and 18 s:
  - SMS bundle: boots straight into the VDP Test menu inside SMSAdvance and is still running.
  - PCE bundle: boots and runs. The full-screen color changes between 15 s and 18 s (96,000 px differ), so the emulated CPU is executing.
  - Both run with the patched header. mGBA does not enforce the complement, but real BIOS does, and it is now correct.
- Save type on the Omega: custom codes are not in the kernel table, so the kernel uses its default 0x10 -> **64 KB SRAM .sav**. That is what both emulators expect, and it is the same as unpatched SMSA/PCEA builds.

## 3. Tool: tool/omega_emu_bundle.py
```
python3 omega_emu_bundle.py ROMS/ ART/ --out OUT --smsadvance smsadvance.gba --pceadvance pceadvance.gba \
   [--imgs SD/SYSTEM/IMGS] [--gba-roms SD/GBA] [--savetable ds-de/source/saveMODE.h] [--map codes.json] [--custom] [--fit pad|crop|stretch]
```
- Output: `OUT/roms/<name>.gba`, plus `OUT/SYSTEM/IMGS/<c0>/<c1>/<CODE>.bmp`.
- With `--custom`, also writes `OUT/SYSTEM/IMGS/CUSTOM/<name>.bmp` for the no-bundle plugin route. It warns when there are more than 256.
- Collision check: before picking a code it reads existing IMGS codes, every .gba code under --gba-roms, the kernel save table, and the codes already used in this run. It then moves to the next free code and logs the change.
  - Tested: with SRRR already present in IMGS, the SMS ROM got SRRS.
- `codes.json`: pass it back as `--map` to keep codes stable when ROMs are added later.
- Also skips same-stem duplicates (.sms/.gg) and anything without its emulator. Inputs are never modified.
- Also reused: omega_art.py (make_bmp/complement).

## 4. Limits / trade-offs
| | Plugin + CUSTOM art | Bundle .gba + IMGS art |
|---|---|---|
| Art limit | **256 per CUSTOM folder** (first 256 in dir order) | Sharded `IMGS/X/Y/`. No practical limit (46k codes per prefix) |
| Start screen / favourites art | No (icon only) | Yes |
| Saves | Kernel writes `<name>.esv` (64 KB) | `<name>.sav` (64 KB SRAM, kernel default for unknown code). Existing .esv saves do not carry over automatically. They are probably the same 64 KB emulator SRAM image, so renaming .esv to .sav should work, but **I have not tested that.** |
| Size per game | ROM only | ROM + 59 KB (SMS) / 47 KB (PCE). Trivial on SD and well under 32 MB, even for 2.5 MB PCE cards |
| Emulator updates | Swap one file in SYSTEM/PLUG | Rebuild all bundles (the script makes this a one-liner) |
| Other | GG BIOS through Pogoshell. Same-stem .sms/.gg share art | Emulator in-ROM menu is the same. PCE CD (.iso) not covered (one CD per build anyway) |
- Folder size: DS Style v7.4 lists up to **512 files per folder**, for either route. Split big sets (e.g. SMS/A-M, SMS/N-Z).

## 5. Recommendation
1. **Under 256 games per system art style: use the plugin + `IMGS/CUSTOM` (and/or `IMGS2/CUSTOM`).** Nothing to rebuild, saves stay as they are, and the code proves it works. Generate the art with `omega_emu_bundle.py --custom` (bundles can be ignored) or with DS Style's own Thumbnail Scraper.
2. Use **bundles** when CUSTOM would go over 256 (CUSTOM is shared with GBA hacks, GB, NES, folders...), or when art should show on the start screen and favourites. Keep codes stable with `codes.json`.
3. Before any wider rollout, test on the cart:
   - Copy one bundle plus its BMP to a spare folder (back up /SYSTEM/IMGS first).
   - Check the art, the boot, and an SRAM save/reload.
   - Optionally try an .esv->.sav rename.
   - Since verified on real hardware by the author (art displays).
