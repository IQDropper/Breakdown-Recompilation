"""evd_dump.py - read Breakdown's area event files (romdata/area/<area>/*.evd) without playing.

Layout (worked out 2026-11-13 from hon_under_A.evd):
  0x00 '.evd'   0x14 u32 event count N   0x18 u32 header size H (0x6c)
  0x30 u32[N]   event offsets; event k starts at H + off[k]
  event:  u32 field count F, then F fields of 40 bytes:
          char name[32], u32 type, u32 value
          type 0 = integer, 1 = float, 3 = string (value = offset from the event start)
Event TYPE strings seen: EV_TYPE_LOAD (LOAD_FILENAME = the area pak it streams in), EV_TYPE_AREAHIT (a box:
POSITION_X/Y/Z + SCALE_X/Y/Z), EV_TYPE_OBSERVE_AND (runs when the named events fire), EV_TYPE_OBJ, ...

Usage:
  python tools/evd_dump.py game_files/romdata/area/hon_under/hon_under_A.evd        all events, all fields
  python tools/evd_dump.py game_files/romdata/area --loads [--md out.md]             loads + triggers, every area
"""
import os
import struct
import sys


def read_evd(path):
    d = open(path, 'rb').read()
    if d[:4] != b'.evd':
        raise ValueError('%s: not an .evd file' % path)
    n, h = struct.unpack_from('<II', d, 0x14)
    offs = struct.unpack_from('<%dI' % n, d, 0x30)
    events = []
    for k, off in enumerate(offs):
        e = h + off
        (f,) = struct.unpack_from('<I', d, e)
        fields = []
        for i in range(f):
            r = e + 4 + i * 40
            name = d[r:r + 32].split(b'\0')[0].decode('latin-1')
            typ, raw = struct.unpack_from('<II', d, r + 32)
            if typ == 1:
                val = round(struct.unpack_from('<f', d, r + 36)[0], 3)
            elif typ == 3:
                val = d[e + raw:d.index(b'\0', e + raw)].decode('latin-1')
            else:
                val = raw
            fields.append((name, typ, val))
        events.append(fields)
    return events


def get(fields, key, default=None):
    return next((v for n, _, v in fields if n == key), default)


def summary(path):
    """One line per load / trigger / observer event."""
    out = []
    for fields in read_evd(path):
        t = get(fields, 'TYPE', '')
        name = get(fields, 'NAME', '?')
        if t == 'EV_TYPE_LOAD':
            out.append('LOAD     %-40s -> %s (type %s, level %s)' % (
                name, get(fields, 'LOAD_FILENAME'), get(fields, 'LOAD_TYPE'), get(fields, 'LOAD_LEVEL')))
        elif t == 'EV_TYPE_AREAHIT':
            p = [get(fields, 'POSITION_' + a, 0) for a in 'XYZ']
            s = [get(fields, 'SCALE_' + a, 0) for a in 'XYZ']
            out.append('TRIGGER  %-40s at (%.0f, %.0f, %.0f) size (%.1f, %.1f, %.1f)' % (name, *p, *s))
        elif t.startswith('EV_TYPE_OBSERVE'):
            # rule: LOOK groups (TYPE, TGT, SUBJECT, VALUE, FLAG) are the conditions, MOVE groups the actions
            conds, acts, cur = [], [], {}
            for n, _, v in fields:
                if n.startswith(('LOOK_', 'MOVE_')):
                    cur[n] = v
                    if n in ('LOOK_FLAG', 'MOVE_TIME'):
                        if 'LOOK_TGT' in cur:
                            conds.append('%s.%s==%s' % (cur['LOOK_TGT'], cur['LOOK_SUBJECT'], cur['LOOK_VALUE']))
                        else:
                            acts.append('%s.%s=%s' % (cur['MOVE_TGT'], cur['MOVE_SUBJECT'], cur['MOVE_VALUE']))
                        cur = {}
            joiner = ' OR ' if t.endswith('_OR') else ' AND '
            out.append('RULE     %-40s if %s then %s' % (name, joiner.join(conds), '; '.join(acts)))
        elif get(fields, 'SAVE_POINT') is not None:
            pass
    return out


def load_map(path):
    """Rows: (load name, target area, level, trigger name, (x,y,z), (sx,sy,sz)) for each load a trigger box starts,
    plus ('SAVE', NAME_ID, None, name, (hx,hy,hz), None) for save points."""
    evs = read_evd(path)
    boxes = {}
    for f in evs:
        if get(f, 'TYPE') == 'EV_TYPE_AREAHIT':
            boxes[get(f, 'NAME')] = (tuple(get(f, 'POSITION_' + a, 0) for a in 'XYZ'),
                                     tuple(get(f, 'SCALE_' + a, 0) for a in 'XYZ'))
    loads = {get(f, 'NAME'): f for f in evs if get(f, 'TYPE') == 'EV_TYPE_LOAD'}
    rows = []
    for f in evs:
        if not str(get(f, 'TYPE', '')).startswith('EV_TYPE_OBSERVE'):
            continue
        looks = [v for n, _, v in f if n == 'LOOK_TGT']
        moves = [v for n, _, v in f if n == 'MOVE_TGT']
        for m in moves:
            if m in loads and get(loads[m], 'LOAD_FILENAME'):
                for lk in looks:
                    if lk in boxes:
                        rows.append((m, get(loads[m], 'LOAD_FILENAME'), get(loads[m], 'LOAD_LEVEL'), lk) + boxes[lk])
    for n, f in loads.items():
        if get(f, 'SAVE_POINT') is not None:
            rows.append(('SAVE', get(f, 'NAME_ID'), None, n,
                         tuple(get(f, 'POSITION_H' + a, 0) for a in 'XYZ'), None))
    return rows


def main():
    a = sys.argv[1:]
    if a and '--map' in a:
        files = sorted(os.path.join(r, f) for r, _, fs in os.walk(a[0]) for f in fs if f.endswith('.evd'))
        lines = ['| File | Kind | Goes to / save id | Box centre (x, y, z) | Box size | Event |', '|---|---|---|---|---|---|']
        for f in files:
            for r in load_map(f):
                rel = os.path.relpath(f, a[0]).replace('\\', '/')
                p = '(%.0f, %.0f, %.0f)' % r[4]
                if r[0] == 'SAVE':
                    lines.append('| %s | save point | %s | %s | | %s |' % (rel, r[1], p, r[3]))
                else:
                    kind = 'NEW AREA (level 3)' if r[2] == 3 else 'section stream (level %s)' % r[2]
                    lines.append('| %s | %s | %s | %s | (%.1f, %.1f, %.1f) | %s |' % (rel, kind, r[1], p, *r[5], r[3]))
        text = '\n'.join(lines)
        if '--md' in a:
            with open(a[a.index('--md') + 1], 'w', encoding='utf-8') as fh:
                fh.write(text + '\n')
        print(text)
        return 0
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    target = a[0]
    if os.path.isfile(target) and '--loads' not in a:
        for k, fields in enumerate(read_evd(target)):
            print('#%d' % k)
            for n, t, v in fields:
                print('   %-24s %s' % (n, v))
        return 0
    files = [target] if os.path.isfile(target) else sorted(
        os.path.join(r, f) for r, _, fs in os.walk(target) for f in fs if f.endswith('.evd'))
    lines = []
    for f in files:
        try:
            s = summary(f)
        except Exception as ex:  # noqa: BLE001 - report and keep going
            s = ['ERROR %s' % ex]
        if s:
            lines.append('## ' + os.path.relpath(f, target if os.path.isdir(target) else os.path.dirname(f)))
            lines += s
    text = '\n'.join(lines)
    if '--md' in a:
        with open(a[a.index('--md') + 1], 'w', encoding='utf-8') as fh:
            fh.write('```\n' + text + '\n```\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
