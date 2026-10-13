#!/usr/bin/env python3
"""omega_art.py - build an EZ-Flash Omega / Omega DE /IMGS thumbnail tree.

Format is taken from the kernel source (ez-flash/omega-de-kernel source/ezkernelnew.c
Load_Thumbnail ~L1591 and draw call ~L2169): the kernel reads 0x4B38 (19,256) bytes
of /IMGS/<c0>/<c1>/<CODE>.bmp and blits 120x80 u16 pixels starting at offset 0x36,
top row first, as native GBA BGR555. So the file is a 54-byte BMP header + 19,200
bytes of 16-bit pixels (top-down) + 2 pad bytes = 19,256.

Only *.gba files get thumbnails (kernel checks the extension). For .gb/.gbc this tool
can optionally build a .gba wrapper = jagoombacolor.gba + ROM (exactly what the kernel
does itself in PSRAM) with a custom game code at 0xAC.

Never modifies input files. Writes everything to --out.
"""
import argparse, json, os, re, string, struct, sys, hashlib
from pathlib import Path
from PIL import Image, ImageOps

W, H, SIZE = 120, 80, 19256
IMG_EXT = ('.png', '.jpg', '.jpeg', '.bmp', '.webp', '.gif')
CODE_RE = re.compile(r'^[A-Z0-9]{4}$')

def make_bmp(img_path, fit='pad'):
    im = Image.open(img_path).convert('RGB')
    if fit == 'stretch':
        im = im.resize((W, H), Image.LANCZOS)
    elif fit == 'crop':
        im = ImageOps.fit(im, (W, H), Image.LANCZOS)
    else:
        im = ImageOps.pad(im, (W, H), Image.LANCZOS, color=(0, 0, 0))
    px = bytearray()
    for r, g, b in (im.get_flattened_data() if hasattr(im,"get_flattened_data") else im.getdata()):          # top-down, as the kernel blits it
        px += struct.pack('<H', (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
    hdr = b'BM' + struct.pack('<IHHI', SIZE, 0, 0, 0x36)
    hdr += struct.pack('<IiiHHIIiiII', 40, W, -H, 1, 16, 0, len(px), 2835, 2835, 0, 0)
    data = hdr + px + b'\0\0'
    assert len(data) == SIZE and len(hdr) == 0x36
    return data

def gba_code(b):
    return b[0xAC:0xB0].decode('latin1') if len(b) >= 0xC0 else ''

def complement(b):
    return (-(sum(b[0xA0:0xBD]) + 0x19)) & 0xFF

def gb_title(b):
    return b[0x134:0x143].split(b'\0')[0].decode('latin1', 'replace').strip()

def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())

def find_cover(rom, covers):
    k = norm(rom.stem)
    return covers.get(k)

def gen_code(name, used):
    """Deterministic custom code: 'Z' + 3 chars from hash, skipping used ones.
    Retail GBA codes start with A/B/C/F/K/P/R/U/V; Z-prefix avoids them."""
    alphabet = string.ascii_uppercase + string.digits
    h = int(hashlib.sha1(name.encode()).hexdigest(), 16)
    for i in range(36 ** 3):
        n = (h + i) % (36 ** 3)
        c = 'Z' + alphabet[n // 1296] + alphabet[(n // 36) % 36] + alphabet[n % 36]
        if c not in used:
            return c
    raise RuntimeError('code space exhausted')

def load_emu(path):
    t = Path(path).read_text()
    if path.endswith('.h'):  # bin2c header from the kernel repo
        body = t.split('{', 1)[1].rsplit('}', 1)[0]
        return bytes(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{2}', body))
    return Path(path).read_bytes()

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('roms'); ap.add_argument('covers'); ap.add_argument('--out', required=True)
    ap.add_argument('--fit', choices=['pad', 'crop', 'stretch'], default='pad')
    ap.add_argument('--hack-list', help='text file of ROM filenames to treat as hacks (re-coded)')
    ap.add_argument('--map', help='JSON {romfilename: CODE} manual overrides')
    ap.add_argument('--emu', help='jagoombacolor.gba or kernel jagoombacolor.h; enables GB/GBC wrapping')
    a = ap.parse_args()
    out = Path(a.out); (out / 'IMGS').mkdir(parents=True, exist_ok=True)
    covers = {norm(p.stem): p for p in Path(a.covers).rglob('*') if p.suffix.lower() in IMG_EXT}
    hacks = set(Path(a.hack_list).read_text().split('\n')) if a.hack_list else set()
    manual = json.loads(Path(a.map).read_text()) if a.map else {}
    emu = load_emu(a.emu) if a.emu else None
    roms = sorted(p for p in Path(a.roms).rglob('*') if p.suffix.lower() in ('.gba', '.gb', '.gbc'))
    report = {'ok': [], 'recoded': [], 'collisions': [], 'no_cover': [], 'skipped': [], 'wrapped': []}
    by_code = {}
    info = []
    for p in roms:
        b = p.read_bytes()
        if p.suffix.lower() == '.gba':
            c = gba_code(b)
            info.append(dict(path=p, kind='gba', code=c, valid=bool(CODE_RE.match(c))))
            if CODE_RE.match(c): by_code.setdefault(c, []).append(p)
        else:
            info.append(dict(path=p, kind='gb', code=None, title=gb_title(b), valid=False))
    used = set(by_code) | set(manual.values())
    # Decide which ROMs need a new code: listed hacks, invalid codes, and all but the
    # first ROM of a shared code (the first keeps it; retail codes are never changed in place).
    for code, ps in by_code.items():
        if len(ps) > 1:
            report['collisions'].append({'code': code, 'files': [x.name for x in ps]})
    seen = set()
    for it in info:
        p = it['path']
        if p.name in manual:
            it['new'] = manual[p.name]
        elif it['kind'] == 'gb':
            it['new'] = gen_code(p.name, used) if emu else None
        elif p.name in hacks or not it['valid'] or it['code'] in seen:
            it['new'] = gen_code(p.name, used)
        else:
            it['new'] = None
        if it.get('new'): used.add(it['new'])
        if it['kind'] == 'gba' and it['valid']: seen.add(it['code'])
    for it in info:
        p = it['path']; final = it['new'] or it['code']
        if it['kind'] == 'gb' and not emu:
            report['skipped'].append(f'{p.name}: .gb/.gbc never gets a thumbnail (kernel only checks *.gba); pass --emu to wrap')
            continue
        if it['new']:
            b = bytearray(emu + p.read_bytes()) if it['kind'] == 'gb' else bytearray(p.read_bytes())
            b[0xAC:0xB0] = final.encode(); b[0xBD] = complement(b)
            dst = out / 'roms' / (p.stem + '.gba'); dst.parent.mkdir(exist_ok=True)
            dst.write_bytes(b)
            report['wrapped' if it['kind'] == 'gb' else 'recoded'].append(
                f'{p.name} -> roms/{dst.name} code {final}' + (f' (was {it["code"]!r})' if it['code'] else ''))
        cov = find_cover(p, covers)
        if not cov:
            report['no_cover'].append(f'{p.name} ({final})'); continue
        d = out / 'IMGS' / final[0] / final[1]; d.mkdir(parents=True, exist_ok=True)
        data = make_bmp(cov, a.fit)
        (d / f'{final}.bmp').write_bytes(data)
        assert (d / f'{final}.bmp').stat().st_size == SIZE
        report['ok'].append(f'{p.name} -> IMGS/{final[0]}/{final[1]}/{final}.bmp')
    (out / 'report.json').write_text(json.dumps(report, indent=2))
    for k, v in report.items():
        print(f'== {k} ({len(v)})'); [print('  ', x) for x in v]

if __name__ == '__main__':
    main()
