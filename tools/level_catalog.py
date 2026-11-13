"""level_catalog.py - one catalog of Breakdown's levels, read from the game files (no playing needed).

For every area, in story order:
  - checkpoints (the game's own table in default.xbe 0x270988: name, chapter) and area changes (NOW LOADING)
  - load trigger boxes (section streaming) and the rules that fire them        (.evd, tools/evd_dump.py)
  - enemy and character spawns: model, roster note, behaviour script, weapon, inventory, section block, position,
    and the story scene they belong to (NAME_ID 's02a03_010_...' -> scene group 's02a' = checkpoints 'save_cp02a_*')
  - hazards: trigger boxes with attack power (AREAHIT_ATKPOW)
  - power-up scenes (behaviour scripts named *pow*/*PowerUp*)

Usage:
  python tools/level_catalog.py --md out.md [--json out.json]
  python tools/level_catalog.py --ahead save_cp02a_05        spawns in that checkpoint's scene group and area
Reads game_files/ (your own copy); writes only the files you name.
"""
import json
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(__file__))
import bdpk  # noqa: E402
import evd_dump as evd  # noqa: E402
import save_checkpoint as sc  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), '..', 'game_files', 'romdata')
ROSTER_VA = 0x248FE4          # 68 rows of 0x34 bytes; +0 = pointer to 'lechara_<model> (<developer note>)'
ROLE = {'SX': 'soldier (rifle)', 'LT': 'laser T\'lan', 'FT': 'heavy T\'lan', 'AT': 'armed T\'lan',
        'WT': 'female T\'lan', 'NT': 'T\'lan', 'HT': 'half-armed T\'lan', 'DN': 'T\'lan (dn)', 'BN': 'boss',
        'R': 'researcher', 'S': 'soldier', 'T': 'T\'lan', 'NV': 'Alex', 'HE': 'Derrick (scene)'}


def roster():
    """{model name: developer note} from the game's character list."""
    d, v2f = sc._xbe()
    out = {}
    o = v2f(ROSTER_VA)
    for i in range(68):
        p = struct.unpack_from('<I', d, o + i * 0x34)[0]
        f = v2f(p)
        s = d[f:d.index(b'\0', f)].decode('cp932', 'replace')
        m = re.match(r'lechara_(\S+?)\s*[ (（　](.*)', s)
        if m:
            out[m.group(1)] = m.group(2).strip(' ()（）')
    return out


def scene_of(name_id):
    m = re.match(r'(?:ai_)?s(\d\d)([a-z]?)', name_id or '')
    return 's%s%s' % (m.group(1), m.group(2)) if m else None


def catalog():
    scripts = bdpk.script_names(ROOT)
    notes = roster()
    cps = sc.checkpoints()
    order = []
    for name, area, _place, chapter in cps:
        if area not in order:
            order.append(area)
    areas = {}
    for area in order + sorted(set(os.listdir(os.path.join(ROOT, 'area'))) - set(order)):
        folder = os.path.join(ROOT, 'area', area)
        if not os.path.isdir(folder):
            continue
        a = areas[area] = {'checkpoints': [], 'loads': [], 'spawns': [], 'hazards': [], 'powerups': []}
        a['checkpoints'] = [{'name': n, 'chapter': c} for n, ar, _p, c in cps if ar == area]
        for f in sorted(os.listdir(folder)):
            if not f.endswith('.evd'):
                continue
            path = os.path.join(folder, f)
            for row in evd.load_map(path):
                if row[0] != 'SAVE':
                    a['loads'].append({'file': f, 'to': row[1], 'level': row[2], 'box': row[3],
                                       'at': row[4], 'size': row[5]})
            for ev in evd.read_evd(path):
                t = evd.get(ev, 'TYPE')
                if t == 'EV_TYPE_CHAR_CREATE':
                    nm = evd.get(ev, 'NAME', '')
                    model = re.sub(r'^obj_|_L_LE_.*$', '', nm)
                    nid = evd.get(ev, 'NAME_ID') or ''
                    ai = scripts.get(evd.get(ev, 'AI_SCRIPT'), str(evd.get(ev, 'AI_SCRIPT')))
                    fam = re.search(r'g_enemy_([a-z]{2})', ai)
                    code = re.search(r'_([A-Z]{1,2})\d*$', nid)
                    a['spawns'].append({
                        'file': f, 'name_id': nid, 'scene': scene_of(nid), 'model': model,
                        'who': notes.get(model, ''), 'role': ROLE.get(code.group(1), '') if code else '',
                        'ai': ai.replace('bhb_', ''), 'behaviour': fam.group(1).upper() if fam else '',
                        'weapon': evd.get(ev, 'WEAPON'), 'inventory': evd.get(ev, 'CHAR_INVENTORY'),
                        'block': evd.get(ev, 'BLOCK'), 'group': evd.get(ev, 'CHAR_GROUP_ID'),
                        'at': [round(evd.get(ev, 'POSITION_' + x, 0)) for x in 'XYZ']})
                elif t == 'EV_TYPE_AREAHIT' and evd.get(ev, 'AREAHIT_ATKPOW'):
                    a['hazards'].append({'file': f, 'name': evd.get(ev, 'NAME'),
                                         'power': evd.get(ev, 'AREAHIT_ATKPOW'),
                                         'type': evd.get(ev, 'AREAHIT_ATKTYPE'),
                                         'at': [round(evd.get(ev, 'POSITION_' + x, 0)) for x in 'XYZ']})
        a['powerups'] = sorted({s for s in scripts.values() if re.search(r'(?i)_pow\d|powerup', s)
                                and scene_of(s.replace('bhb_ai_', '').replace('bhb_mo_', ''))
                                in {scene_of(c['name'].replace('save_cp', 's').replace('save_cpai', 's'))
                                    for c in a['checkpoints']}})
    return areas


def cp_scene(cp):
    return scene_of(re.sub(r'^save_cp(ai)?', 's', cp))


def ahead(areas, cp):
    for area, a in areas.items():
        if any(c['name'] == cp for c in a['checkpoints']):
            sc_ = cp_scene(cp) or ''
            same = [s for s in a['spawns'] if s['scene'] == sc_]
            rest = [s for s in a['spawns'] if not s['scene']]
            return area, sc_, same, rest
    return None, None, [], []


def md(areas):
    out = ['# Breakdown level catalog', '',
           'Generated by tools/level_catalog.py from the game files. Story order. "Level 3" loads change area '
           '(NOW LOADING); "level 4" loads stream the next section quietly.', '']
    for area, a in areas.items():
        enemies = [s for s in a['spawns'] if s['behaviour'] or s['role'] not in ('', 'Alex', 'researcher')]
        out += ['## %s' % area, '',
                '- Checkpoints (%d): %s' % (len(a['checkpoints']), ', '.join(
                    '%s (ch %d)' % (c['name'], c['chapter']) for c in a['checkpoints']) or 'none'),
                '- Loads: ' + ('; '.join('%s -> %s (level %s) box %s size %s' % (
                    l['file'], l['to'], l['level'], tuple(l['at']), tuple(l['size'])) for l in a['loads']) or 'none'),
                '- Spawns: %d characters, %d with enemy behaviour scripts' % (len(a['spawns']), len(enemies)),
                '- Hazards: %d boxes with attack power' % len(a['hazards'])]
        if a['powerups']:
            out.append('- Power-up scenes: ' + ', '.join(a['powerups']))
        out += ['', '| Scene | Spawn id | Model | Who (dev note) | Behaviour script | Weapon | Inv | Block | Position |',
                '|---|---|---|---|---|---|---|---|---|']
        for s in sorted(a['spawns'], key=lambda s: (s['scene'] or 'zz', s['name_id'])):
            out.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (
                s['scene'] or '-', s['name_id'], s['model'], s['who'], s['ai'], s['weapon'], s['inventory'],
                s['block'], tuple(s['at'])))
        out.append('')
    return '\n'.join(out) + '\n'


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    areas = catalog()
    if '--ahead' in a:
        cp = a[a.index('--ahead') + 1]
        area, scene, same, rest = ahead(areas, cp)
        print('%s: area %s, scene group %s' % (cp, area, scene))
        for s in same + rest:
            print('  %-28s %-34s %-22s %-28s weapon %s block %s at %s' % (
                s['name_id'], s['model'], s['role'] or s['who'][:22], s['ai'], s['weapon'], s['block'], s['at']))
        return 0
    if '--md' in a:
        open(a[a.index('--md') + 1], 'w', encoding='utf-8').write(md(areas))
    if '--json' in a:
        json.dump(areas, open(a[a.index('--json') + 1], 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    for area, x in areas.items():
        print('%-11s checkpoints %3d  loads %2d  spawns %3d  hazards %3d  powerups %d' % (
            area, len(x['checkpoints']), len(x['loads']), len(x['spawns']), len(x['hazards']), len(x['powerups'])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
