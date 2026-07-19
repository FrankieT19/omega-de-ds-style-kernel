# DS Style v7.2 Enhanced Cheats Test

## For testers

This optional experimental build expands the cheat system in DS Style v7.2 for
the EZ-Flash Omega Definitive Edition. It is separate from the standard DS
Style release and is intended for public hardware testing before any decision
is made about including the changes normally.

Existing DS Style cheat packs continue to work unchanged. You do not need to
convert them. The enhanced format adds broader support for converted
CodeBreaker, GameShark and Action Replay-style operations, including
conditions, button activators, wider writes, arithmetic, pointers, fills,
slides and guarded ROM patches.

Back up `SAVER` and `RTS` before testing. Use disposable ROM and save copies for
ROM-patch tests. This build is for the Omega Definitive Edition only and must
not be installed on the original Omega.

The release ZIP contains installation guidance, a hardware test checklist and
a concise format reference. Reinstall the standard DS Style v7.2 kernel at any
time to leave the experiment.

## What changed

- Preserved the existing EZ-Flash byte-list syntax, including comma and colon
  forms used by the bundled cheat library.
- Added `W8`, `W16` and `W32` writes.
- Added conditional, masked conditional and button-activated operations.
- Added arithmetic, pointer, fill and slide operations.
- Added guarded ROM patching for PSRAM and NOR launches.
- Added explicit `[Group|MULTI]` groups while preserving the existing
  zero-or-one behavior of plain `[Group]` sections.
- Added transactional validation so malformed or excessive enhanced commands
  are rejected rather than partly installed.

## Safety limits

- 128 selected menu entries.
- 128 runtime records.
- 512 writes per update.
- 16 nested conditions.
- 32 ROM groups.
- 128 ROM condition bytes.
- 256 ROM patch bytes.

ROM commands alter the launched game image. PSRAM changes are temporary. A
game written to NOR remains patched until it is deleted or rewritten. Invalid
cheats can crash a game, alter game state or damage a save, so testing should
always use backups.

The original Omega is not included because its kernel image has substantially
less available space. A converter is not bundled or required by the normal DS
Style cheat packs. Enhanced cheat creation remains an optional external
workflow.

## Compatibility verification

The bundled DS Style v7.2 cheat library was audited without conversion:

- 2,491 `.cht` files.
- 195,275 lines.
- 22,710 plain group headers.
- 147,588 stock comma-form rows.
- 70 stock colon-form rows.
- No enhanced-format rows in the standard pack.

The experimental kernel was built with devkitARM and its packaged binary was
verified byte-for-byte against the successful build output.

## Credit

Enhanced cheat-engine work is based on changes shared by
[SkillerCMP](https://github.com/SkillerCMP). Thank you for adapting and
contributing this work to DS Style.
