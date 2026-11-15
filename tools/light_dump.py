"""light_dump.py - list every light in Breakdown's level files (romdata/area/<area>/*.evd), no playing needed.

Lights are EV_TYPE_LIGHT events. The game reads them in 0x316A0 (LightEv_Create) with the defaults used below
when a field is missing, so this tool shows what the game actually gets. Field meanings: docs/formats/lighting.md.

Usage:
  python tools/light_dump.py game_files/romdata/area/hon_under             every light, one line each, per file
  python tools/light_dump.py game_files/romdata/area --summary             count by kind, per area
  python tools/light_dump.py game_files/romdata/area/hon_under --csv out.csv
  python tools/light_dump.py ... --md out.md                                same text as a Markdown file
  python tools/light_dump.py ... --kind FOG,SHADOW                          only these kinds
Reads your own game files; writes only the files you name.
"""
import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from evd_dump import read_evd  # noqa: E402

# LIGHT_KIND -> name. 0..14 are the game's own names (table at 0x241270, used by the LIGHT_MANAGER debug page);
# 15..23 are named from the event names the level tool gave them (lelight_gloweffect_..., lelight_tfog_...).
KIND = {0: 'POINT', 1: 'DIRECT', 2: 'AMBIENT', 3: 'DIRECT_BLK', 4: 'AMBIENT_BLK', 5: 'AMBIENT_CTRL',
        6: 'FOG', 7: 'FOG_FAR', 8: 'FOG_BLK', 9: 'FOG_FAR_BLK', 10: 'SHADOW', 11: 'SHADOW_BLK',
        12: 'TLIGHT', 13: 'TLIGHT_BLK', 14: 'SPOTEFFECT', 15: 'GLOW', 16: 'SHADOW_EX', 17: 'GLOW_LITE',
        18: 'OVERLAY', 19: 'OVERLAY_BLK', 20: 'TFOG', 21: 'TFOG_BLK', 22: 'KIND22', 23: 'WATERREF'}
# What the game does with each kind (short; see lighting.md).
ROLE = {'POINT': 'lamp, lights nearby objects', 'DIRECT': 'room sun/key light', 'AMBIENT': 'room fill colour',
        'DIRECT_BLK': 'section key light', 'AMBIENT_BLK': 'section fill colour',
        'AMBIENT_CTRL': 'added to room fill', 'FOG': 'room near fog', 'FOG_FAR': 'room far fog',
        'FOG_BLK': 'section near fog', 'FOG_FAR_BLK': 'section far fog', 'SHADOW': 'room shadow direction',
        'SHADOW_BLK': 'section shadow direction', 'SHADOW_EX': 'extra shadow box', 'TLIGHT': 'room tint/flash',
        'TLIGHT_BLK': 'section tint/flash', 'SPOTEFFECT': 'visible light cone', 'GLOW': 'glow sprite',
        'GLOW_LITE': 'small glow sprite', 'OVERLAY': 'screen overlay colour', 'OVERLAY_BLK': 'section overlay',
        'TFOG': 'haze/depth filter', 'TFOG_BLK': 'section haze filter', 'WATERREF': 'water reflection',
        'KIND22': '?'}
# Kinds the game ties to the room (cell) they stand in (0x165F0); the rest are section-wide or, for POINT,
# picked by distance from the whole section's list (0x16740).
ROOM_KINDS = {1, 2, 5, 6, 7, 10, 12, 14, 15, 18, 20, 23}
FLAG = {0: 'POST', 1: 'PRE'}   # table 0x2412B0; PRE = already baked into the room's vertex colours


def lights(path):
    """Yield one dict per light in an .evd, with the game's defaults filled in."""
    for ev in read_evd(path):
        f = {}
        for n, _, v in ev:
            f.setdefault(n, v)          # the game's lookup takes the first match
        if f.get('TYPE') != 'EV_TYPE_LIGHT':
            continue
        g = lambda k, d: f.get(k, d)   # noqa: E731
        kind = int(g('LIGHT_KIND', 0))
        L = {
            'name': g('NAME', '?'), 'kind': kind, 'kind_name': KIND.get(kind, 'KIND%d' % kind),
            'flag': int(g('LIGHT_FLAG', 0)), 'mode': int(g('MODE', 0)),
            'col': (g('LIGHT_COL_R', 1.0), g('LIGHT_COL_G', 1.0), g('LIGHT_COL_B', 1.0)),
            'pos': (g('POSITION_X', 0.0), g('POSITION_Y', 0.0), g('POSITION_Z', 0.0)),
            'dir': (g('LIGHT_DIR_X', 0.0), g('LIGHT_DIR_Y', 0.0), g('LIGHT_DIR_Z', 0.0)),
            'amb': g('LIGHT_AMBIENT', 1.0), 'dif': g('LIGHT_DIFFUSE', 1.0), 'spec': g('LIGHT_SPECULAR', 1.0),
            'near': g('LIGHT_NEAR', 10.0), 'far': g('LIGHT_FAR', 60.0),
            'fog_near': g('FOG_NEAR', 0.0), 'fog_far': g('FOG_FAR', 1.0),
            'blink': int(g('LIGHT_BL_TYPE', 0)), 'blink_speed': g('LIGHT_BL_SPEED', 1.0),
            'scale': (g('SCALE_X', None), g('SCALE_Y', None), g('SCALE_Z', None)),
            'id': g('NAME_ID', ''), 'extra': {},
        }
        x = L['extra']
        if kind in (20, 21):                       # haze filter
            x['power'] = g('EFF_LG_ALPHA', 100.0)
            x['pitch'] = g('EFF_LG_PITCH', 100.0)
        if kind == 14:                             # light cone
            x['length'] = g('EFF_LG_LENGTH', 30.0)
            x['pitch'] = g('EFF_LG_PITCH', 1.0)
            x['alpha'] = g('EFF_LG_ALPHA', 0.8)
            x['angle'] = g('EFF_LG_ANGLE', 30.0)
            x['projection_map'] = g('EFF_LG_SW_PROJECTION_MAP', 0)
        if kind == 23:                             # water reflection
            x['alpha'] = g('EFF_LG_ALPHA', 0.6)
            x['angle'] = g('EFF_LG_ANGLE', 25.0)
        if kind in (15, 17):                       # glow sprite
            x['texture'] = g('EFF_TEXTUREID', 0)
            x['alpha'] = g('EFF_LG_ALPHA', 1.0)
            x['mask'] = g('EFF_LG_MASK', 0.1)
            x['offset'] = g('EFF_LG_OFFSET', 5.0)
        yield L


def fmt3(v):
    return '(%s)' % ', '.join('%.3g' % c if isinstance(c, (int, float)) else '-' for c in v)


def line(L):
    k = L['kind_name']
    s = '%-34s %-12s %-4s col%s' % (L['name'], k, FLAG.get(L['flag'], L['flag']), fmt3(L['col']))
    if L['kind'] not in (2, 4, 5, 19) and any(L['pos']):
        s += ' at%s' % fmt3(L['pos'])
    if L['kind'] in (1, 3, 10, 11, 14, 15, 16, 17, 20, 21, 23):
        s += ' dir%s' % fmt3(L['dir'])
    if L['kind'] in (0, 1, 3, 10, 11, 12, 13, 15, 17, 18, 19, 20, 21):
        s += ' amb/dif/spec %.3g/%.3g/%.3g' % (L['amb'], L['dif'], L['spec'])
    if L['kind'] in (0, 6, 7, 8, 9, 16, 20, 21, 23):
        s += ' near/far %.4g/%.4g' % (L['near'], L['far'])
    if L['kind'] in (20, 21):
        s += ' fog %.4g/%.4g' % (L['fog_near'], L['fog_far'])
    if L['blink']:
        s += ' blink type %d speed %.3g' % (L['blink'], L['blink_speed'])
    if L['extra']:
        s += ' ' + ' '.join('%s=%.4g' % (a, b) for a, b in L['extra'].items())
    return s


def evd_files(target):
    if os.path.isfile(target):
        return [target]
    return sorted(os.path.join(r, f) for r, _, fs in os.walk(target) for f in fs if f.endswith('.evd'))


def main():
    a = sys.argv[1:]
    if not a or a[0].startswith('-'):
        print(__doc__)
        return 1
    target = a[0]
    base = target if os.path.isdir(target) else os.path.dirname(target)
    only = set(a[a.index('--kind') + 1].upper().split(',')) if '--kind' in a else None
    rows = []
    for f in evd_files(target):
        rel = os.path.relpath(f, base).replace('\\', '/')
        for L in lights(f):
            if only and L['kind_name'] not in only:
                continue
            rows.append((rel, L))

    if '--csv' in a:
        with open(a[a.index('--csv') + 1], 'w', newline='', encoding='utf-8') as fh:
            w = csv.writer(fh)
            w.writerow(['file', 'name', 'kind', 'kind_name', 'flag', 'r', 'g', 'b', 'x', 'y', 'z', 'dx', 'dy', 'dz',
                        'ambient', 'diffuse', 'specular', 'near', 'far', 'fog_near', 'fog_far', 'blink',
                        'blink_speed', 'extra'])
            for rel, L in rows:
                w.writerow([rel, L['name'], L['kind'], L['kind_name'], FLAG.get(L['flag'], L['flag']), *L['col'],
                            *L['pos'], *L['dir'], L['amb'], L['dif'], L['spec'], L['near'], L['far'],
                            L['fog_near'], L['fog_far'], L['blink'], L['blink_speed'],
                            ' '.join('%s=%s' % kv for kv in L['extra'].items())])

    out = []
    if '--summary' in a:
        per = collections.defaultdict(collections.Counter)
        for rel, L in rows:
            per[rel.split('/')[0] if '/' in rel else os.path.basename(base)][L['kind_name']] += 1
        order = [KIND[k] for k in sorted(KIND)]
        for area in sorted(per):
            c = per[area]
            out.append('%-12s %4d  ' % (area, sum(c.values())) +
                       ', '.join('%s %d' % (k, c[k]) for k in order if c[k]))
        tot = collections.Counter(L['kind_name'] for _, L in rows)
        out.append('%-12s %4d  ' % ('ALL', len(rows)) + ', '.join('%s %d' % (k, tot[k]) for k in order if tot[k]))
    else:
        cur = None
        for rel, L in rows:
            if rel != cur:
                cur = rel
                n = sum(1 for r, _ in rows if r == rel)
                out.append('## %s  (%d lights)' % (rel, n))
            out.append('  ' + line(L))
    text = '\n'.join(out)
    if '--md' in a:
        with open(a[a.index('--md') + 1], 'w', encoding='utf-8') as fh:
            fh.write('```\n' + text + '\n```\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
