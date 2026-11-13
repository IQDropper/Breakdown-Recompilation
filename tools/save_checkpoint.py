"""save_checkpoint.py - list Breakdown's checkpoints and write a copy of a save that starts at any of them.

The game's checkpoint table (192 entries, story order) is read from default.xbe at 0x270988: 16 bytes each,
(name pointer, place id, place id + 261, chapter). Names are area starts ("hon_under", "bettou", ...) or
checkpoints ("save_cp02a_02" ...). Checkpoint i belongs to the last area name above it.

A save (UDATA/4e4d0009/<slot>/data.sav, 0x1FC50 bytes) holds the area path at 0x90 ("area/hon_under") and the
checkpoint name at 0x120. Its signature (0x1FC30, 20 bytes) is HMAC-SHA1 over bytes 0..0x1FC30 with the
recomp's console key (16 zero bytes, external/xboxrecomp kernel_bridge.c XboxSignatureKey).

Also set: the table index (0x08, must match the name or the slot shows DAMAGED GAME), the area check value
(0x140 = romdata/area/vsn[area id]), the section (0xB8) and the story-scene pack (0xE0, --block).
Story flags, weapons and powers in the save stay as they were, so a jump far ahead can start with missing items.

Status 2026-11-13: an edited save passes the game's checks and the jump to 'bettou' shows NOW LOADING and loads
bettou_A, then stays on a black screen (no player). The missing piece is probably the story-scene pack or story
flags for that checkpoint. Not usable for test slots yet.

Usage:
  python tools/save_checkpoint.py --list                         every checkpoint with its area and chapter
  python tools/save_checkpoint.py IN.sav OUT.sav save_cp02b_02   copy IN to OUT at that checkpoint (re-signed)
  python tools/save_checkpoint.py --check FILE.sav               show area/checkpoint and whether the signature is good
Never point OUT at your real save folder; test with RECOMP_SAVE_DIR=<copy>.
"""
import hashlib
import hmac
import os
import struct
import sys

XBE = os.path.join(os.path.dirname(__file__), '..', 'game_files', 'default.xbe')
TABLE_VA = 0x270988
SIG_AT, SAVE_SIZE = 0x1FC30, 0x1FC50
INDEX_AT = 0x08                      # u32 table index of the checkpoint (must match the name)
AREA_AT, SECTION_AT, BLOCK_AT, CP_AT, FIELD = 0x90, 0xB8, 0xE0, 0x120, 32
# 0xB8 = loaded section ("area/hon_under_B"), 0xE0 = story-scene pack ("area/hon_under/block_B_s02a02_100_p")
SIZES = {AREA_AT: 0x28, SECTION_AT: 0x28, BLOCK_AT: 0x40, CP_AT: 0x20}   # text field sizes (to the next field)
VSN_AT = 0x140                       # u32 area check value = romdata/area/vsn[area id]
AREAS_VA = 0x2439D8                  # area table: 36-byte rows, name[16] inline, u32 id at +0x10
KEY = b'\0' * 16


def _xbe():
    d = open(XBE, 'rb').read()
    base = struct.unpack_from('<I', d, 0x104)[0]
    n, sh = struct.unpack_from('<II', d, 0x11C)
    secs = [struct.unpack_from('<6I', d, sh - base + i * 0x38)[1:5] for i in range(n)]

    def v2f(v):
        for va, vs, ra, rs in secs:
            if va <= v < va + rs:
                return ra + v - va
        raise ValueError(hex(v))
    return d, v2f


def area_ids():
    """{area name: id} from the game's area table (opening 0, hon_under 1, ... ending 22)."""
    d, v2f = _xbe()
    o, ids = v2f(AREAS_VA), {}
    for i in range(32):
        row = d[o + i * 36:o + i * 36 + 36]
        name = row[:16].split(b'\0')[0].decode('latin-1')
        if not name.replace('_', '').isalnum() or not name.islower():
            break
        ids[name] = struct.unpack_from('<I', row, 0x10)[0]
    return ids


def vsn_value(area):
    v = open(os.path.join(os.path.dirname(XBE), 'romdata', 'area', 'vsn'), 'rb').read()
    return struct.unpack_from('<I', v, 4 * area_ids()[area])[0]


def checkpoints():
    """[(name, area, place, chapter)] in story order."""
    d, v2f = _xbe()
    o, rows, area = v2f(TABLE_VA), [], None
    while True:
        p, a, _b, c = struct.unpack_from('<4I', d, o)
        if p == 0:
            break
        s = v2f(p)
        name = d[s:d.index(b'\0', s)].decode('latin-1')
        if not name.startswith('save_'):
            area = name
        rows.append((name, area, a, c))
        o += 16
    return rows


def sign(buf):
    buf[SIG_AT:SIG_AT + 20] = hmac.new(KEY, bytes(buf[:SIG_AT]), hashlib.sha1).digest()


def check(buf):
    def txt(at):
        return bytes(buf[at:at + FIELD]).split(b'\0')[0].decode('latin-1')
    ok = hmac.new(KEY, bytes(buf[:SIG_AT]), hashlib.sha1).digest() == bytes(buf[SIG_AT:SIG_AT + 20])
    return txt(AREA_AT), txt(CP_AT), ok


def section_of(area, name):
    """The section whose .evd holds the save point with NAME_ID == name, else the first section (area/<area>_A)."""
    sys.path.insert(0, os.path.dirname(__file__))
    import evd_dump
    folder = os.path.join(os.path.dirname(XBE), 'romdata', 'area', area)
    for f in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if f.endswith('.evd') and f[:-4] != area:
            for ev in evd_dump.read_evd(os.path.join(folder, f)):
                if evd_dump.get(ev, 'NAME_ID') == name:
                    return 'area/' + f[:-4]
    # save points are listed in the area's main .evd, not a section's: fall back to the first section
    if os.path.exists(os.path.join(folder, area + '_A.evd')):
        return 'area/%s_A' % area
    return 'area/' + area


def set_checkpoint(buf, name, block=''):
    rows = checkpoints()
    idx = next((i for i, r in enumerate(rows) if r[0] == name), None)
    if idx is None:
        raise SystemExit('unknown checkpoint %s (see --list)' % name)
    area = rows[idx][1]
    struct.pack_into('<I', buf, INDEX_AT, idx)
    struct.pack_into('<I', buf, VSN_AT, vsn_value(area))
    for at, val in ((AREA_AT, 'area/' + area), (SECTION_AT, section_of(area, name)), (BLOCK_AT, block),
                    (CP_AT, name)):
        b, size = val.encode(), SIZES[at]
        if len(b) >= size:
            raise SystemExit('%r is too long for its %d-byte field' % (val, size))
        buf[at:at + size] = b + b'\0' * (size - len(b))
    assert len(buf) == SAVE_SIZE
    sign(buf)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    if a[0] == '--list':
        for i, (n, ar, p, c) in enumerate(checkpoints()):
            print('%3d  chapter %2d  %-10s %s' % (i, c, ar, n))
        return 0
    if a[0] == '--check':
        print('area=%s checkpoint=%s signature_ok=%s' % check(bytearray(open(a[1], 'rb').read())))
        return 0
    src, dst, name = a[:3]
    buf = bytearray(open(src, 'rb').read())
    if len(buf) != SAVE_SIZE:
        raise SystemExit('%s: %d bytes, expected %d' % (src, len(buf), SAVE_SIZE))
    if os.path.abspath(dst) == os.path.abspath(src):
        raise SystemExit('refusing to overwrite the input save')
    set_checkpoint(buf, name, a[a.index('--block') + 1] if '--block' in a else '')
    open(dst, 'wb').write(buf)
    print('wrote %s: area=%s checkpoint=%s signature_ok=%s' % ((dst,) + check(buf)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
