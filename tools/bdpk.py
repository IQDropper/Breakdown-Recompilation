"""bdpk.py - list the entries of Breakdown's .pak files (magic 'BDPK').

Layout (worked out 2026-11-13): header 'BDPK', u32 entry count, ...; entry headers are 0x80-byte records
  'BD', u16 kind, u32 id, u32 size, u32 0, u32 offset (from the file start), name (ASCII, NUL-terminated) at +0x14
Behaviour scripts (kind 3) start 'BHEV': chunks of 4-char tag + u32 length (UPDT date, NAME, ATR, DATA, PROG...).
Kinds: 0 = animations ('gym_...'), 1 = models ('obj_...'), 2 = textures ('nut_...', TIM2), 3 = behaviour scripts
('bhb_ai_...' AI, 'bhb_mo_...' motion), 4 = sounds ('snd_...'). The level files (.evd) refer to scripts by id:
CHAR_CREATE AI_SCRIPT / MOT_SCRIPT = a kind-3 id.

Usage:
  python tools/bdpk.py game_files/romdata/common/base.pak          entries of one pack
  python tools/bdpk.py game_files/romdata --kind 3                   every script in every pack
"""
import os
import re
import struct
import sys

KINDS = {0: 'anim', 1: 'model', 2: 'texture', 3: 'script', 4: 'sound'}
_ENTRY = re.compile(rb'BD(..)(....)(....)(....)(....)([A-Za-z_][ -~]{2,90})\x00', re.S)


def entries(path):
    """[(kind, id, size, offset, name)] for one pack."""
    d = open(path, 'rb').read()
    if d[:4] != b'BDPK':
        return []
    (n,) = struct.unpack_from('<I', d, 4)
    out = []
    for m in _ENTRY.finditer(d):
        if m.start() % 0x40:          # headers are 0x40-aligned; skips matches inside data
            continue
        k = struct.unpack('<H', m.group(1))[0]
        if k not in KINDS:
            continue
        i, sz, off = (struct.unpack('<I', m.group(g))[0] for g in (2, 3, 5))
        out.append((k, i, sz, off, m.group(6).decode('latin-1')))
        if len(out) >= n:
            break
    return out


def script_names(root):
    """{script id: name} over every pack under root (game_files/romdata)."""
    names = {}
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith('.pak'):
                for k, i, _sz, _off, name in entries(os.path.join(r, f)):
                    if k == 3:
                        names.setdefault(i, name)
    return names


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    kind = int(a[a.index('--kind') + 1]) if '--kind' in a else None
    files = [a[0]] if os.path.isfile(a[0]) else sorted(
        os.path.join(r, f) for r, _, fs in os.walk(a[0]) for f in fs if f.endswith('.pak'))
    for f in files:
        for k, i, sz, off, name in entries(f):
            if kind is None or k == kind:
                print('%-8s %5d %9d  %-50s %s' % (KINDS[k], i, sz, name, os.path.basename(f)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
