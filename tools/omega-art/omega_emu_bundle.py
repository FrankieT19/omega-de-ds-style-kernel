#!/usr/bin/env python3
"""omega_emu_bundle.py - give SMS/GG/SG and PC Engine games box art on EZ-Flash Omega
(DS Style or stock kernel) by bundling each ROM into its own .gba with a unique game code.

  ROM folder + art folder  ->  OUT/roms/<name>.gba  (emulator + 1 ROM, new code at 0xAC,
                                                     header complement at 0xBD fixed)
                               OUT/SYSTEM/IMGS/<c0>/<c1>/<CODE>.bmp (120x80, 19,256 B)
                               OUT/SYSTEM/IMGS/CUSTOM/<name>.bmp   (--custom: plugin route)

Bundle formats follow the emulators' own builders:
  SMSAdvance v2.5 Formats.txt: 64-byte header "SMS",1A | size | flags | ... | title[32]
  PCEAdvance (patters' pceadvance_compile.py): "<31sc5I12s" header, EMUID "NES",1A
Codes: 'S'+3 for SMS/GG/SG, 'T'+3 for PCE, base36 from SHA-1 of the ROM filename stem.
Never reuses a code that is already in --imgs (existing SYSTEM/IMGS), in .gba files under
--gba-roms, in the kernel save table (--savetable saveMODE.h), or earlier in this run.
Inputs are never modified.
"""
import argparse, hashlib, json, re, string, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from omega_art import make_bmp, complement, norm, CODE_RE, IMG_EXT, SIZE

SMS_EXT = {'.sms': 0, '.gg': 4, '.sg': 0}
PCE_EXT = {'.pce'}
A36 = string.ascii_uppercase + string.digits

def gen_code(prefix, name, used):
    h = int(hashlib.sha1(norm(name).encode()).hexdigest(), 16)
    for i in range(36 ** 3):
        n = (h + i) % 36 ** 3
        c = prefix + A36[n // 1296] + A36[n // 36 % 36] + A36[n % 36]
        if c not in used:
            return c
    raise RuntimeError('code space exhausted for prefix ' + prefix)

def sms_bundle(emu, rom, ext, title):
    flags = SMS_EXT[ext]
    if '(e)' in title.lower() or '(europe)' in title.lower(): flags |= 0   # leave NTSC; user toggles in menu
    if '(j)' in title.lower() or '(japan)' in title.lower(): flags |= 2
    hdr = b'SMS\x1a' + struct.pack('<IHBBIB15x', len(rom), flags, 0, 0, 0, 0) + title.encode('ascii', 'replace')[:31].ljust(32, b'\0')
    assert len(hdr) == 64
    return emu + hdr + rom

def pce_bundle(emu, rom, title):
    rom = rom + b'\0' * (-len(rom) % 4)
    flags = 4 if ('(u)' in title.lower() or '(usa)' in title.lower()) else 0
    hdr = struct.pack('<31sc5I12s', title.encode('ascii', 'replace')[:31], b'\0', len(rom) + 16, flags, 0, 0, 0x1A53454E, b'@' + b' ' * 11)
    return emu + hdr + rom

def patch_header(b, code):
    b = bytearray(b); b[0xAC:0xB0] = code.encode(); b[0xBD] = complement(b); return bytes(b)

def existing_codes(a):
    used, src = set(), {}
    if a.imgs:
        for p in Path(a.imgs).glob('*/*/*.bmp'):
            c = p.stem.upper()
            if CODE_RE.match(c): used.add(c); src.setdefault(c, f'IMGS:{p.name}')
    if a.gba_roms:
        for p in Path(a.gba_roms).rglob('*.gba'):
            with open(p, 'rb') as f:
                f.seek(0xAC); c = f.read(4).decode('latin1')
            if CODE_RE.match(c): used.add(c); src.setdefault(c, f'rom:{p.name}')
    if a.savetable:
        for c in re.findall(r'\{"([A-Z0-9]{4})"', Path(a.savetable).read_text()):
            used.add(c); src.setdefault(c, 'kernel save table')
    return used, src

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('roms'); ap.add_argument('art'); ap.add_argument('--out', required=True)
    ap.add_argument('--smsadvance', help='smsadvance.gba (v2.5)'); ap.add_argument('--pceadvance', help='pceadvance.gba (v7.5)')
    ap.add_argument('--imgs', help='existing SYSTEM/IMGS folder to avoid code collisions with')
    ap.add_argument('--gba-roms', help='existing GBA ROM folder to avoid code collisions with')
    ap.add_argument('--savetable', help="DS Style/Omega kernel source/saveMODE.h (retail code list)")
    ap.add_argument('--map', help='JSON {romfilename: CODE} manual overrides (kept stable across runs)')
    ap.add_argument('--custom', action='store_true', help='also write SYSTEM/IMGS/CUSTOM/<stem>.bmp for unbundled plugin use')
    ap.add_argument('--fit', choices=['pad', 'crop', 'stretch'], default='pad')
    a = ap.parse_args()
    out = Path(a.out); (out / 'roms').mkdir(parents=True, exist_ok=True)
    emu = {'sms': Path(a.smsadvance).read_bytes() if a.smsadvance else None,
           'pce': Path(a.pceadvance).read_bytes() if a.pceadvance else None}
    art = {norm(p.stem): p for p in Path(a.art).rglob('*') if p.suffix.lower() in IMG_EXT}
    manual = json.loads(Path(a.map).read_text()) if a.map else {}
    used, src = existing_codes(a)
    rep = {'built': [], 'no_art': [], 'skipped': [], 'collisions_avoided': [], 'manual_conflicts': [], 'custom': []}
    codes_out = {}
    stems = {}
    for p in sorted(Path(a.roms).rglob('*')):
        ext = p.suffix.lower()
        kind = 'sms' if ext in SMS_EXT else 'pce' if ext in PCE_EXT else None
        if not kind: continue
        if emu[kind] is None:
            rep['skipped'].append(f'{p.name}: no --{kind}advance given'); continue
        if norm(p.stem) in stems:
            rep['skipped'].append(f'{p.name}: same name as {stems[norm(p.stem)]} (output/.bmp names would clash)'); continue
        stems[norm(p.stem)] = p.name
        rom = p.read_bytes(); title = p.stem
        if p.name in manual:
            code = manual[p.name]
            if code in used: rep['manual_conflicts'].append(f'{p.name}: {code} already used by {src.get(code, "this run")}')
        else:
            code = gen_code('S' if kind == 'sms' else 'T', title, used)
            first = gen_code('S' if kind == 'sms' else 'T', title, set())
            if code != first: rep['collisions_avoided'].append(f'{p.name}: {first} taken by {src.get(first, "this run")} -> {code}')
        used.add(code); src[code] = f'this run:{p.name}'
        data = patch_header(sms_bundle(emu[kind], rom, ext, title) if kind == 'sms' else pce_bundle(emu[kind], rom, title), code)
        dst = out / 'roms' / f'{p.stem}.gba'; dst.write_bytes(data)
        codes_out[p.name] = code
        cov = art.get(norm(p.stem))
        line = f'{p.name} -> roms/{dst.name} [{code}] {len(data)//1024} KB'
        if cov:
            bmp = make_bmp(cov, a.fit)
            d = out / 'SYSTEM' / 'IMGS' / code[0] / code[1]; d.mkdir(parents=True, exist_ok=True)
            (d / f'{code}.bmp').write_bytes(bmp)
            line += f' + IMGS/{code[0]}/{code[1]}/{code}.bmp'
            if a.custom:
                c = out / 'SYSTEM' / 'IMGS' / 'CUSTOM'; c.mkdir(parents=True, exist_ok=True)
                (c / f'{p.stem}.bmp').write_bytes(bmp); rep['custom'].append(f'CUSTOM/{p.stem}.bmp')
        else:
            rep['no_art'].append(p.name)
        rep['built'].append(line)
    if len(rep['custom']) > 256:
        rep['skipped'].append(f'WARNING: {len(rep["custom"])} CUSTOM images; DS Style only indexes the first 256')
    (out / 'codes.json').write_text(json.dumps(codes_out, indent=2))   # reuse as --map for stable codes
    (out / 'report.json').write_text(json.dumps(rep, indent=2))
    for k, v in rep.items():
        print(f'== {k} ({len(v)})'); [print('  ', x) for x in v]

if __name__ == '__main__':
    main()
