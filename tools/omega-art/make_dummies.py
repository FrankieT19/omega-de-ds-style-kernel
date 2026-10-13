import struct, os
from pathlib import Path
from PIL import Image
r = Path('test/roms'); c = Path('test/covers'); r.mkdir(parents=True, exist_ok=True); c.mkdir(parents=True, exist_ok=True)
def gba(name, title, code):
    b = bytearray(0x200); b[0xA0:0xAC] = title.ljust(12, '\0').encode()[:12]; b[0xAC:0xB0] = code.encode()
    b[0xB2] = 0x96; b[0xBD] = (-(sum(b[0xA0:0xBD]) + 0x19)) & 0xFF; (r / name).write_bytes(b)
def gb(name, title, cgb):
    b = bytearray(0x8000); b[0x134:0x134+len(title)] = title.encode(); b[0x143] = 0x80 if cgb else 0; (r / name).write_bytes(b)
gba('FireRed.gba', 'POKEMON FIRE', 'BPRE'); gba('Unbound.gba', 'POKEMON FIRE', 'BPRE')   # hack sharing retail code
gba('Homebrew.gba', '', '\0\0\0\0'); gba('Fusion.gba', 'METROID4USA', 'AMTE')
gb('Tetris.gb', 'TETRIS', False); gb('Oracle.gbc', 'ZELDA', True)
for i, n in enumerate(['FireRed', 'Unbound', 'Homebrew', 'Tetris', 'Oracle']):
    Image.new('RGB', (300, 420), ((i * 60) % 256, 255 - i * 40, 128)).save(c / f'{n}.png')
