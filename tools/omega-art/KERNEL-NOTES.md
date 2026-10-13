# EZ-Flash Omega / Omega DE thumbnails: GB/GBC and ROM hacks (2026-10-12)

## 1. Kernel lookup logic (source read directly)
Repos: github.com/ez-flash/omega-kernel (last commit ea5cc4e "k1.10", 2024-09-06) and
github.com/ez-flash/omega-de-kernel (12cb68b "k1.06", 2025-09-08). Both are Apache-2.0. I found no public source for the DE **B** kernel (1.02), so these findings only cover the two repos.

- `Load_Thumbnail()`: omega-de-kernel `source/ezkernelnew.c` L1591-1616 / omega-kernel `source/ezkernel.c` L1452-1477.
  It opens the ROM file, seeks to **0xAC**, reads 4 bytes (the GBA game code), and builds
  `/IMGS/%c/%c/%c%c%c%c.bmp`. It does not change the case or validate the bytes, so the code must match the file and folder names exactly. It reads **0x4B38 = 19,256 bytes**.
- Caller: DE L2082 / original L1757: `if(!strcasecmp(&(pfilename_pic[strlengba-3]), "gba"))`. This is the only path that calls `Load_Thumbnail`. **So .gb/.gbc (and .nes) files never get a thumbnail lookup.** Drawing is also gated on `is_GBA` (DE L2166).
- A GBA file with no header code still has its 4 bytes at 0xAC read (often 00 or garbage). The kernel tries a matching path, which fails, and then draws `gImage_NOTFOUND` (DE L2172).
- Draw: DE L2169 `DrawPic((u16*)(pReadCache+0x10036),120,80,...)`. The pixels start at offset 0x36 and are **16-bit, top row first, native GBA BGR555**. The BMP header is ignored.
  **Correction to the earlier research:** the file is not 24-bit. 54-byte header + 19,200 bytes of pixels (120×80×2) + 2 pad bytes = 19,256 bytes. The 24-bit RGB stored as 3 bytes per pixel would be 28,800 bytes. "Red and blue swapped" means BMP 16-bit stores blue in the low bits, while the GBA stores red there. The 2018 GBAtemp thread backs up the 16-bit size: a tool output of 19,254 bytes vs Photoshop's 19,256.
- How GB/GBC run: DE L1452-1458. The kernel copies the built-in `jagoombacolor_gba` (source/jagoombacolor.h, 76,412 bytes) into PSRAM and appends the raw ROM. So a pre-built ".gba" made of jagoombacolor + ROM gives the emulator the same image the kernel builds in PSRAM. One part is untested: that file then loads through the normal GBA path (save type and patching), not the GB path.

## 2. RHEA and community
- RHEA = "ROM Header Editor Advance", by **HackMew** (Andrea Sartori). https://www.romhacking.net/utilities/542/, v1.0.1, 2007, Windows, freeware closed source (no license stated). The github.com/sterophonick/rhea link in ChimeraGaming's guide returns 404.
- RHEA is GBA-only and does not handle GB/GBC. The ChimeraGaming guide warns that RHEA breaks Goomba-built .gba files (white screen) and can rename every ROM in a folder.
- GBAtemp thread 510210 (Drumaster, 2018) covers the format and the 19,256-byte check. Axel-MaV made "EZΩ Thumbmaker" (one image at a time). Apple_Blossom shared a box-art pack (2022). timdmackey was working on a theme with custom thumbnail sizes. Hat-Man has a video thread (638563).
- ChimeraGaming/EZ-Flash_Guide shows that the Goomba Front + custom serial at 0xAC method works on DE.

## 3. Verdict
- (a) GB/GBC: **does not work today as .gb/.gbc files** (hard extension gate). **Workaround that works with no kernel change:** wrap each ROM into a .gba (Goomba/jagoombacolor + ROM) with a unique code at 0xAC. The community has confirmed this on DE. Trade-offs: the Goomba save and savestate behavior differs (one GBAtemp report of savestates failing), and each game uses more space.
- (b) Hacks: **works today**. They only need a unique code. If a hack shares a code (for example BPRE), it shows the retail art.
- Kernel patch alternative: about 10-20 lines. In the `else` branch at DE L2082, for gb/gbc, read the cart title at 0x134 or use a hash of the filename and build `/IMGS/GB/<name>.bmp`. You would also need to set `is_GBA`, because it gates drawing. Building needs devkitARM plus the repo's Windows-only `Link_kernel_image.exe`. Flashing is the normal kernel update from SD. The firmware/FPGA stays separate, and you can reflash the official kernel. That makes it realistic but not zero-risk, and it is not needed given the wrapper route. I have not built or tested it.

## 4. Tool: omega_art.py
`python3 omega_art.py ROMS/ COVERS/ --out OUT [--hack-list hacks.txt] [--map map.json] [--emu jagoombacolor.gba|.h] [--fit pad|crop|stretch]`
- Matches covers to ROMs by normalized filename. Writes `OUT/IMGS/X/Y/CODE.bmp` in the exact kernel format and checks that each file is 19,256 bytes.
- Retail codes are kept. Hacks listed in hacks.txt, ROMs with an invalid or blank code, and ROMs that duplicate a code (every one after the first) get a deterministic `Z???` code. The tool never picks a code that is already used or mapped. It writes patched copies to `OUT/roms/` (0xAC plus fixed complement check at 0xBD) and never touches the originals.
- With `--emu` (you can point it at the kernel repo's jagoombacolor.h), .gb/.gbc ROMs are wrapped into `OUT/roms/<name>.gba` with a new code.
- `report.json` lists ok, recoded, collisions, no_cover, skipped and wrapped.
- Tested on dummy headers (tool/make_dummies.py, test/): all 5 BMPs are 19,256 bytes, the BPRE collision was flagged and recoded, the blank homebrew code was recoded, GB/GBC were wrapped, and the missing cover was reported.

### Testing on the cart (since done by the author: art works)
1. Copy one recoded hack and its IMGS entry to a spare SD folder. Back up the existing /IMGS first.
2. On the SD tab, press SELECT and check the art appears with correct colors and the right way up.
3. Do the same for one wrapped GB game: check it boots, then check SRAM save and reload. Try savestates last.

## 5. Contacts / novelty
- Kernel: EZ-Flash team (github.com/ez-flash issues; ezflash.cn). There is no named individual maintainer.
- RHEA: HackMew (inactive since about 2014; site andreasartori.net/hackmew).
- Novelty: there are existing single-image tools (Thumbmaker) and manual guides. A batch tool that is cross-platform, handles collisions and auto-recodes hacks, wraps GB/GBC, and uses the format checked against the kernel source looks new. It would be worth sharing on GBAtemp thread 510210 or as a GitHub repo once it is verified on hardware. Packaging jagoombacolor is fine under Apache-2.0 with attribution, but GB ROMs must never be bundled.
