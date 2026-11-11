"""Install the DS Style v8.1 return hook without changing the cartridge header."""

import argparse
from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory


def install(original, code, clear_offset):
    if len(original) < 0x18a or original[:4] != bytes.fromhex("2e0000ea"):
        raise ValueError("Unsupported kernel header")
    if original[0xc0:0xc4] != bytes.fromhex("060000ea"):
        raise ValueError("Unsupported ARM CRT entry vector")
    if original[0xe0:0xe4] != bytes.fromhex("0103a0e3"):
        raise ValueError("Unsupported ARM CRT startup")
    if original[0x126:0x130] != bytes.fromhex("40210903c80100f02cf8"):
        raise ValueError("Unsupported CRT EWRAM clear")

    for marker, address in [(0x12345678, 0x080000e4), (0x23456789, 0x08000189)]:
        marker = struct.pack("<I", marker)
        if code.count(marker) != 1:
            raise ValueError("Invalid boot hook relocation")
        code = code.replace(marker, struct.pack("<I", address))

    offset = (len(original) + 3) & ~3
    displacement = offset + clear_offset - 0x130
    if displacement & 1 or not -0x400000 <= displacement < 0x400000:
        raise ValueError("Boot hook is out of range")
    data = bytearray(original.ljust(offset, b"\0") + code)
    struct.pack_into("<I", data, 0xe0, 0xea000000 | ((offset - 0xe8) // 4))
    struct.pack_into("<HH", data, 0x12c,
                     0xf000 | ((displacement >> 12) & 0x7ff),
                     0xf800 | ((displacement >> 1) & 0x7ff))
    return bytes(data)


def patch_rom(rom, source, arm_bin):
    with TemporaryDirectory() as directory:
        obj = Path(directory) / "boot.o"
        raw = obj.with_suffix(".bin")

        def run(tool, *args):
            name = arm_bin / ("arm-none-eabi-" + tool)
            if name.with_suffix(".exe").is_file():
                name = name.with_suffix(".exe")
            return subprocess.check_output([name, *args], text=True)

        run("as", "-mcpu=arm7tdmi", source, "-o", obj)
        run("objcopy", "-O", "binary", "-j", ".text", obj, raw)
        entries = run("nm", "-n", obj).splitlines()
        offset = int(next(line.split()[0] for line in entries
                          if line.endswith(" Slot_ClearWorkRam")), 16)
        data = install(rom.read_bytes(), raw.read_bytes(), offset)
        rom.write_bytes(data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("arm_bin", type=Path)
    args = parser.parse_args()
    patch_rom(args.rom, Path(__file__).with_name("boot-entry.s"), args.arm_bin)
