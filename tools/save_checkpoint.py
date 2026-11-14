"""save_checkpoint.py - list Breakdown's checkpoints and write a copy of a save that starts at any of them.

The game's checkpoint table (192 entries) is read from default.xbe at 0x270988: 16 bytes each,
(name pointer, place id, place id + 261, chapter). Names are area starts ("hon_under", "bettou", ...) or
checkpoints ("save_cp02a_02" ...). Checkpoint i belongs to the last area name above it. The checkpoint numbers are
NOT story order inside an area (cp02a_05 comes before cp02a_02); the chapter column is.

A save (UDATA/4e4d0009/<slot>/data.sav, 0x1FC50 bytes) is laid out in docs/formats/save_story.md. This tool sets:
  0x08 table index (must match the name or the slot shows DAMAGED GAME), 0x90 area, 0xB8 section, 0xE0 story-scene
  pack, 0x120 checkpoint name, 0x140 area check value (romdata/area/vsn[area id]);
  0x59C4 the 1000 story numbers (all 0, as in every real save seen so far) and 0x6964 the 2048 story flags, from
  tools/checkpoint_progress.json (made by tools/story_flags.py from the game's scripts);
  0x0C / 0x0E event-state list counts = 0 and the saved-object counts 0x6A98 / 0x6AA8 / 0x6AB8 = 0, so the level
  starts from its own defaults instead of another area's event states;
  Derrick's position (saved-object block + 0xC, 0x94E0 in real saves) = the checkpoint's trigger box, on the floor;
  Derrick's AI script id (hero block + 0x2E, u16) = the area's chapter control script, as in real checkpoint saves
  (in game a jump also worked with the input's mid-scene script kept, so this is for matching real saves);
  the 0x1400-byte MI_EVENT list at 0x7C30 is emptied with the event lists.
A slot folder also needs its SaveMeta.xbx ("Name=GAME nn"), or the load menu shows the slot as EMPTY: copy the
input slot's folder and replace data.sav, then set the Name line.
Weapons and powers stay as they were in the input save. Signature (0x1FC30, 20 bytes) = HMAC-SHA1 over 0..0x1FC30
with the recomp's console key (16 zero bytes, external/xboxrecomp kernel_bridge.c XboxSignatureKey).

Usage:
  python tools/save_checkpoint.py --list                         every checkpoint with its area and chapter
  python tools/save_checkpoint.py --progress                     what will be written for each checkpoint
  python tools/save_checkpoint.py IN.sav OUT.sav save_cp02b_02   copy IN to OUT at that checkpoint (re-signed)
      --block PACK      story-scene pack instead of the derived one     --keep-flags  leave flags/numbers as in IN
      --keep-events     leave IN's event-state and saved-object lists   --keep-pos    leave Derrick where IN had him
      --keep-ai         leave Derrick's AI script id as in IN
  python tools/save_checkpoint.py --check FILE.sav               area/checkpoint, flags on, signature ok
Never point OUT at your real save folder; test with RECOMP_SAVE_DIR=<copy>.
"""
import hashlib
import hmac
import os
import struct
import sys

def _game_files():
    """game_files/ next to the repo; a git worktree under .claude/worktrees/ looks in the main checkout."""
    if os.environ.get('BREAKDOWN_GAME_FILES'):
        return os.environ['BREAKDOWN_GAME_FILES']
    d = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
    while True:
        if os.path.exists(os.path.join(d, 'game_files', 'default.xbe')):
            return os.path.join(d, 'game_files')
        up = os.path.dirname(d)
        if up == d:
            return os.path.join(os.path.dirname(__file__), '..', 'game_files')
        d = up


XBE = os.path.join(_game_files(), 'default.xbe')
TABLE_VA = 0x270988
SIG_AT, SAVE_SIZE = 0x1FC30, 0x1FC50
INDEX_AT = 0x08                      # u32 table index of the checkpoint (must match the name)
AREA_AT, SECTION_AT, BLOCK_AT, CP_AT, FIELD = 0x90, 0xB8, 0xE0, 0x120, 32
# 0xB8 = loaded section ("area/hon_under_B"), 0xE0 = story-scene pack ("area/hon_under/block_B_s02a02_100_p")
SIZES = {AREA_AT: 0x28, SECTION_AT: 0x28, BLOCK_AT: 0x40, CP_AT: 0x20}   # text field sizes (to the next field)
VSN_AT = 0x140                       # u32 area check value = romdata/area/vsn[area id]
AREAS_VA = 0x2439D8                  # area table: 36-byte rows, name[16] inline, u32 id at +0x10
KEY = b'\0' * 16
VARS_AT, NVARS = 0x59C4, 1000        # story numbers (Global_Get/Set, script commands 760/759)
FLAGS_AT, FLAG_BYTES = 0x6964, 0x100  # story flags, bit i&7 of byte i>>3 (Global_IsOn / flag set, commands 770/769)
SAVE_FLAG = 2007                     # written by the save code itself (0x12AB80 != 0); kept from the input save
EVENTS_N_AT = 0x0C                   # u16 x2: 12-byte event-state records at 0x144, 0x84-byte records at 0x4944
OBJ_COUNTS = (0x6A98, 0x6AA8, 0x6AB8)  # saved objects put back by event id on load (0x28930 / 0x28A00 / 0x28A90)
BLOB_AT, HERO_OFF_AT = 0x7C30, 0x6A70  # saved-object data; u32 at 0x6A70 = offset of Derrick's block in it
MI_SIZE_AT = 0x6A6C                  # u32 size of the MI_EVENT list at the start of the saved-object data
HERO_AI = 0x2E                       # u16 in Derrick's block: his AI script id (+0x2C = motion script, 11)
PROGRESS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'checkpoint_progress.json')


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


def progress():
    import json
    with open(PROGRESS) as f:
        return json.load(f)['checkpoints']


def flags_of(buf):
    return [i for i in range(FLAG_BYTES * 8) if buf[FLAGS_AT + (i >> 3)] >> (i & 7) & 1]


def set_checkpoint(buf, name, block='', keep_flags=False, keep_events=False, keep_pos=False, keep_ai=False):
    rows = checkpoints()
    idx = next((i for i, r in enumerate(rows) if r[0] == name), None)
    if idx is None:
        raise SystemExit('unknown checkpoint %s (see --list)' % name)
    area = rows[idx][1]
    p = progress().get(name, {})
    struct.pack_into('<I', buf, INDEX_AT, idx)
    struct.pack_into('<I', buf, VSN_AT, vsn_value(area))
    section = p.get('section') or section_of(area, name)
    block = block or p.get('block', '')
    for at, val in ((AREA_AT, 'area/' + area), (SECTION_AT, section), (BLOCK_AT, block), (CP_AT, name)):
        b, size = val.encode(), SIZES[at]
        if len(b) >= size:
            raise SystemExit('%r is too long for its %d-byte field' % (val, size))
        buf[at:at + size] = b + b'\0' * (size - len(b))
    if not keep_flags and 'flags' in p:
        keep = SAVE_FLAG in flags_of(buf)
        buf[VARS_AT:VARS_AT + 4 * NVARS] = bytes(4 * NVARS)
        buf[FLAGS_AT:FLAGS_AT + FLAG_BYTES] = bytes(FLAG_BYTES)
        for i in p['flags'] + ([SAVE_FLAG] if keep else []):
            buf[FLAGS_AT + (i >> 3)] |= 1 << (i & 7)
    if not keep_events:
        struct.pack_into('<hh', buf, EVENTS_N_AT, 0, 0)
        for at in OBJ_COUNTS:
            struct.pack_into('<I', buf, at, 0)
        mi = struct.unpack_from('<I', buf, MI_SIZE_AT)[0]           # MI_EVENT list (0xC8E30), put back by 0xC8F60
        if 0 < mi <= struct.unpack_from('<I', buf, HERO_OFF_AT)[0]:
            buf[BLOB_AT:BLOB_AT + mi] = bytes(mi)
    hero = BLOB_AT + struct.unpack_from('<I', buf, HERO_OFF_AT)[0]
    if hero + HERO_AI + 2 > SIG_AT:
        raise SystemExit('Derrick\'s block offset (0x6A70) looks wrong in this save')
    if not keep_pos and 'position' in p:
        struct.pack_into('<3f', buf, hero + 0xC, *p['position'])     # Derrick (0x94E0 in real saves)
        struct.pack_into('<3f', buf, hero + 0x18, *p['position'])    # Alex (0x94EC), next to him
    if not keep_ai and p.get('hero_ai'):
        struct.pack_into('<H', buf, hero + HERO_AI, p['hero_ai'])
    assert len(buf) == SAVE_SIZE
    sign(buf)
    return p


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    if a[0] == '--list':
        for i, (n, ar, p, c) in enumerate(checkpoints()):
            print('%3d  chapter %2d  %-10s %s' % (i, c, ar, n))
        return 0
    if a[0] == '--progress':
        for n, e in progress().items():
            print('%3d %-9s %-16s ch%-2d %-6s %-16s %-42s %-18s flags=%d' % (
                e['index'], e['area'], n, e['chapter'], e['method'], e.get('scene', '-'), e.get('block', '-'),
                e.get('section', '-'), len(e['flags'])))
        return 0
    if a[0] == '--check':
        buf = bytearray(open(a[1], 'rb').read())
        print('area=%s checkpoint=%s signature_ok=%s' % check(buf))
        print('scene pack=%s section=%s flags on=%s' % (
            bytes(buf[BLOCK_AT:BLOCK_AT + 0x40]).split(b'\0')[0].decode('latin-1'),
            bytes(buf[SECTION_AT:SECTION_AT + 0x28]).split(b'\0')[0].decode('latin-1'), flags_of(buf)))
        return 0
    src, dst, name = a[:3]
    buf = bytearray(open(src, 'rb').read())
    if len(buf) != SAVE_SIZE:
        raise SystemExit('%s: %d bytes, expected %d' % (src, len(buf), SAVE_SIZE))
    if os.path.abspath(dst) == os.path.abspath(src):
        raise SystemExit('refusing to overwrite the input save')
    p = set_checkpoint(buf, name, a[a.index('--block') + 1] if '--block' in a else '', '--keep-flags' in a,
                       '--keep-events' in a, '--keep-pos' in a, '--keep-ai' in a)
    open(dst, 'wb').write(buf)
    print('wrote %s: area=%s checkpoint=%s signature_ok=%s' % ((dst,) + check(buf)))
    hero = BLOB_AT + struct.unpack_from('<I', buf, HERO_OFF_AT)[0]
    ai = struct.unpack_from('<H', buf, hero + HERO_AI)[0]          # what was really written
    print('  scene=%s pack=%s flags=%d (%s) hero AI=%d%s' % (
        p.get('scene', '-'), p.get('block', '-'), len(p.get('flags', [])), p.get('method', 'no progress entry'),
        ai, ' (%s)' % p.get('hero_ai_name') if ai == p.get('hero_ai') else ' (kept from IN)'))
    if 'position' not in p:
        print('  no trigger box for %s: Derrick stays where the input save had him' % name)
    return 0


if __name__ == '__main__':
    sys.exit(main())
