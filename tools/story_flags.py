"""story_flags.py - work out Breakdown's story flags for every checkpoint, from the game's own scripts (P4.7).

How the game tracks story progress (docs/formats/save_story.md):
  * a store of 2048 on/off flags and 1000 numbers (task 0x2A8E04), saved at 0x6964 (flags) and 0x59C4 (numbers);
  * each chapter has a hero control script bhb_ai_sNN_HE. Its state 'FlagCheck' walks the scene flags in story
    order ('wake', 'tut1', 'tut2', ...): for the first scene whose 'done' flag is off it loads that scene's block
    pack (cmd_982/cmd_866) and, once Derrick walks into the scene's trigger box (cmd_848 == 2), switches to
    'Change_<scene>_AI', which hands control to the scene's own AI script (cmd_678). That scene script sets the
    scene's 'done' flag (cmd_769(flag, 1)) and hands back.

This tool replays that loop without the game: it runs FlagCheck in a small AAS2 interpreter (Global_IsOn answered
from the current flag set, every trigger box reported as touched), takes the scene it picks, switches on the flags
that scene's scripts set with constants, and repeats. The result is the story as a list of scenes, each with the
flags that are on when it starts. Checkpoints are then placed on that list (save_checkpoint.py --progress).

Usage:
  python tools/story_flags.py                       the scene list, every chapter
  python tools/story_flags.py --json tools/checkpoint_progress.json   the per-checkpoint table save_checkpoint.py uses
"""
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import aas2_dis as A  # noqa: E402
import bdpk  # noqa: E402
import evd_dump  # noqa: E402

# game commands used here (config/aas2_natives.csv)
C_STATE_CHANGE, C_FLAG_SET, C_FLAG_ON, C_VAR_SET, C_VAR_GET = 19, 769, 770, 759, 760
C_BLOCK_FIND, C_BLOCK_LOAD, C_EV_EXISTS, C_EV_MODE, C_SET_AI, C_CPAI = 982, 866, 865, 848, 678, 1519
DEBUG_FLAGS = set(range(2000, 2048))   # 2000-2005 debug starts (Pad_Held), 2007 written by the save code itself
SCENE_ID = re.compile(r'_(s\d\d[a-z]?\d\d_\d\d\d)_')
C_AREA_ID = 871               # byte 0x29C5F5 = id of the area being played (romdata area table)

# area order of the story and the chapter control script each area runs (bhb_ai_sNN_HE in the area's pack)
AREAS = ['opening', 'hon_under', 'bettou', 'hon_hon', 'plant', 'labo1', 'labo2', 'shaft', 'kyodo1', 'gesui',
         'tunnel2', 'park', 'hill', 'silo', 'future', 'future2', 'park2', 'hill2', 'silo2', 'axis', 'park3', 'ending']


def game_files():
    if os.environ.get('BREAKDOWN_GAME_FILES'):
        return os.environ['BREAKDOWN_GAME_FILES']
    d = os.path.abspath(os.path.join(HERE, '..'))
    while True:
        if os.path.exists(os.path.join(d, 'game_files', 'default.xbe')):
            return os.path.join(d, 'game_files')
        up = os.path.dirname(d)
        if up == d:
            return os.path.join(HERE, '..', 'game_files')
        d = up


ROM = os.path.join(game_files(), 'romdata')


class Script:
    """One BHEV script: states by number and name, each with its start/run/stop programs."""

    def __init__(self, name, data):
        self.name = name
        top = A.parse(data)
        self.root = top[0]
        self.states = []          # [(name, {'start': aas2, 'run': aas2, 'stop': aas2})]
        self.subs = []
        for tag, i, nm, part, aas2 in A.programs(self.root):
            lst = self.states if tag == 'STAT' else self.subs
            while len(lst) <= i:
                lst.append(['', {}])
            lst[i][0] = nm
            lst[i][1][part] = aas2

    def state_index(self, name):
        return next((i for i, (n, _) in enumerate(self.states) if n == name), None)

    def all_programs(self):
        for _, parts in self.states + self.subs:
            for a in parts.values():
                yield a

    def const_calls(self, cmd):
        """Every call of game command cmd with its (constant where known) arguments, in file order."""
        for a in self.all_programs():
            for pc, op, arg, ln, extra in A.walk(a):
                if op == 'CAL' and arg - A.NATIVE_BASE == cmd:
                    yield extra


def load_scripts():
    """{id: Script} and {name: id} for every script, first pack wins (packs list the shared ones again)."""
    by_id, ids, packs = {}, {}, {}
    for r, _, fs in sorted(os.walk(ROM)):
        for f in sorted(fs):
            if not f.endswith('.pak'):
                continue
            p = os.path.join(r, f)
            d = None
            for k, i, sz, off, name in bdpk.entries(p):
                if k != 3:
                    continue
                packs.setdefault(name, []).append(os.path.relpath(p, ROM).replace('\\', '/'))
                if i in by_id:
                    continue
                d = d or open(p, 'rb').read()
                try:
                    by_id[i] = Script(name, d[off:off + sz])
                    ids[name] = i
                except ValueError:
                    pass
    return by_id, ids, packs


class Halt(Exception):
    pass


def run(aas2, flags, vars_, log, limit=20000, touched=2, area_id=0):
    """Run one program once. flags/vars_ are read and written. log collects (what, value) events.
    Returns the state number given to State_Change, or None."""
    code = aas2.find('CODE')
    if code is None:
        return None
    ctbl = aas2.find('CTBL')
    ins = A.decode(code.data)
    at = {pc: k for k, (pc, *_r) in enumerate(ins)}
    ints = [0] * 64
    st = []
    k = 0
    steps = 0

    def push(v):
        st.append(v)
        if isinstance(v, int):
            ints[0] = v

    while 0 <= k < len(ins):
        steps += 1
        if steps > limit:
            raise Halt('step limit')
        pc, op, arg, ln = ins[k]
        nxt = k + 1
        if op in ('PSHIN', 'PSHINC', 'PSHINS'):
            push(arg)
        elif op in ('PSHIN0', 'PSHIN1'):
            push(int(op[-1]))
        elif op in ('PSHS', 'PSHSC'):
            st.append(A.cstr(ctbl.data if ctbl else None, arg))
        elif op in ('PSHID', 'PSHIDC'):
            st.append(('var', arg))
        elif op == 'PSHI':
            push(ints[arg] if 0 <= arg < 64 else 0)
        elif re.match(r'PSHI[0-7]$', op):
            push(ints[int(op[-1])])
        elif op.startswith('PSHF') or op.startswith('PSHFN'):
            st.append(0.0)
        elif op == 'POP':
            del st[len(st) - max(arg, 0):]
        elif op.startswith('POP'):
            del st[len(st) - int(op[3:]):]
        elif op == 'MV_I1_I0':
            ints[1] = ints[0]
        elif op in ('CMP_I1_IN', 'CMP_I1_INC', 'CMP_I1_INS'):
            ints[0] = ints[1] - arg
        elif op == 'END':
            break
        elif op in A.ABS_JUMPS or op in A.REL_JUMPS:
            target = arg if op in A.ABS_JUMPS else pc + ln + arg
            base = op.rstrip('CS') if op not in ('BRA', 'BRAC') else 'BRA'
            take = True
            if base in ('JPZ', 'BRZ'):
                take = ints[0] == 0
            elif base in ('JPNZ', 'BRNZ'):
                take = ints[0] != 0
            if take:
                nxt = at[target]
        elif op == 'CAL' and arg < A.NATIVE_BASE:
            o = A.CAL_OPS.get(arg, '')
            a = st[-2] if len(st) >= 2 else 0
            b = st[-1] if st else 0
            ai = a if isinstance(a, int) else 0
            bi = b if isinstance(b, int) else 0
            r = None
            if o == 'set' and isinstance(a, tuple):
                ints[a[1]] = bi
                r = bi
            elif o in ('preinc', 'postinc', 'predec', 'postdec'):
                if isinstance(b, tuple):
                    old = ints[b[1]]
                    ints[b[1]] += 1 if o.endswith('inc') else -1
                    r = ints[b[1]] if o.startswith('pre') else old
            elif o in A.BINOPS and not o.startswith('f'):
                sym = A.BINOPS[o]
                r = {'+': ai + bi, '-': ai - bi, '*': ai * bi, '/': int(ai / bi) if bi else 0,
                     '==': int(ai == bi), '<': int(ai < bi), '<=': int(ai <= bi), '>': int(ai > bi),
                     '>=': int(ai >= bi), '!=': int(ai != bi), '%': ai % bi if bi else 0, '&': ai & bi,
                     '|': ai | bi, '^': ai ^ bi, '<<': ai << (bi & 31), '>>': ai >> (bi & 31),
                     '&&': int(bool(ai) and bool(bi)), '||': int(bool(ai) or bool(bi))}.get(sym)
            elif o == 'neg':
                r = -bi
            elif o == 'not':
                r = ~bi
            elif o == 'lnot':
                r = int(not bi)
            if r is not None and not o.startswith('f'):
                ints[0] = r
        elif op == 'CAL':
            n = arg - A.NATIVE_BASE
            cnt = len(A.ARGTYPES.get(n, ''))
            args = st[len(st) - cnt:] if cnt else []
            r = 0
            if n == C_FLAG_ON:
                r = int(args[0] in flags)
                log.append(('ask', args[0]))
            elif n == C_FLAG_SET:
                (flags.add if args[1] else flags.discard)(args[0])
                log.append(('flag', args[0], args[1]))
            elif n == C_VAR_GET:
                r = vars_.get(args[0], 0)
            elif n == C_VAR_SET:
                vars_[args[0]] = args[1]
                log.append(('var', args[0], args[1]))
            elif n == C_BLOCK_FIND:
                r = -1
            elif n == C_BLOCK_LOAD:
                log.append(('block', args[0]))
            elif n == C_EV_EXISTS:
                r = 1
            elif n == C_EV_MODE:
                r = touched
                log.append(('trigger', args[0]))
            elif n == C_SET_AI:
                log.append(('ai', args[0]))
            elif n == C_CPAI:
                log.append(('cpai', args[0]))
            elif n == C_AREA_ID:
                r = area_id
            elif n == C_STATE_CHANGE:
                return args[0]
            ints[0] = r if isinstance(r, int) else 0
        k = nxt
    return None


def flag_check(script, flags, vars_, area_id):
    """Run FlagCheck (start, then run) once. -> (state index or None, log)."""
    i = script.state_index('FlagCheck')
    log = []
    for part in ('start', 'run'):
        a = script.states[i][1].get(part)
        if a is not None:
            s = run(a, set(flags), dict(vars_), log, area_id=area_id)
            if s is not None:
                return s, log
    return None, log


def scene_writes(by_id, first, chapter_id, seen=None):
    """Constant flag and number writes of a scene's AI script and every AI script it hands to (not back to the
    chapter script). -> ({flag: set of values}, {var: last value}, [script names])."""
    seen = set() if seen is None else seen
    fl, vs, names = {}, {}, []
    todo = [first]
    while todo:
        sid = todo.pop()
        if sid in seen or sid == chapter_id or sid not in by_id:
            continue
        seen.add(sid)
        sc = by_id[sid]
        names.append(sc.name)
        for args in sc.const_calls(C_FLAG_SET):
            if len(args) == 2 and isinstance(args[0], int) and isinstance(args[1], int):
                fl.setdefault(args[0], set()).add(1 if args[1] else 0)
        for args in sc.const_calls(C_VAR_SET):
            if len(args) == 2 and isinstance(args[0], int) and isinstance(args[1], int):
                vs[args[0]] = args[1]
        for args in sc.const_calls(C_SET_AI):
            if args and isinstance(args[0], int):
                todo.append(args[0])
    return fl, vs, names


def replay(by_id, ids, chapter_name, flags, vars_, area_id, max_scenes=80):
    """Walk one chapter script's scenes in order. Yields scene dicts; flags/vars_ are updated in place."""
    cid = ids[chapter_name]
    sc = by_id[cid]
    if sc.state_index('FlagCheck') is None:
        return
    seen_states = []
    for _ in range(max_scenes):
        s, log = flag_check(sc, flags, vars_, area_id)
        if s is None:
            break
        name = sc.states[s][0] if 0 <= s < len(sc.states) else '?'
        blocks = [e[1] for e in log if e[0] == 'block']
        trig = [e[1] for e in log if e[0] == 'trigger']
        cpai = [e[1] for e in log if e[0] == 'cpai']
        before = sorted(flags)
        # what the picked state itself does, then the scene scripts it hands to
        slog = []
        st_flags, st_vars = set(flags), dict(vars_)
        for part in ('start', 'run'):
            a = sc.states[s][1].get(part)
            if a is not None:
                try:
                    if run(a, st_flags, st_vars, slog, area_id=area_id) is not None:
                        break
                except Halt:
                    break
        ai = [e[1] for e in slog if e[0] == 'ai']
        fl, vs, names = {}, {}, []
        for x in ai:
            f2, v2, n2 = scene_writes(by_id, x, cid)
            for f, vals in f2.items():
                fl.setdefault(f, set()).update(vals)
            vs.update(v2)
            names += n2
        # the scene's other characters (Alex 'NV', researchers 'R0', soldiers ...) share its id 'sNNxNN_NNN'
        keys = {m.group(1) for n in names for m in [SCENE_ID.search(n)] if m}
        for other, oid in ids.items():
            m = SCENE_ID.search(other)
            if m and m.group(1) in keys and other not in names:
                f2, v2, n2 = scene_writes(by_id, oid, cid)
                for f, vals in f2.items():
                    fl.setdefault(f, set()).update(vals)
                names += n2
        new_on = set(e[1] for e in slog if e[0] == 'flag' and e[2]) | {f for f, v in fl.items() if v == {1}}
        new_on -= set(DEBUG_FLAGS)
        scene = {'chapter': chapter_name, 'state': name, 'block': blocks[-1] if blocks else '',
                 'trigger': trig[-1] if trig else '', 'cpai': cpai, 'scripts': names,
                 'flags_before': before, 'sets': sorted(new_on), 'vars_set': vs}
        yield scene
        if (name, tuple(before)) in seen_states:
            break
        seen_states.append((name, tuple(before)))
        grow = new_on - flags
        if not grow:
            # the scene's own flags were already on, or it only writes flags both ways: try those once
            grow = {f for f, v in fl.items() if 1 in v} - flags
            if not grow:
                break
        flags |= grow
        vars_.update(vs)


def area_ids():
    import save_checkpoint
    return save_checkpoint.area_ids()


def story(by_id, ids, packs):
    """Every scene of the story in order: replays each area's chapter script(s) with that area's id."""
    aid = area_ids()
    flags, vars_ = set(), {}
    out = []
    for area in AREAS:
        chs = sorted(n for n, pl in packs.items() if re.match(r'bhb_ai_s\d\d_HE$', n)
                     and any(p.startswith('area/%s/%s.pak' % (area, area)) for p in pl))
        for ch in chs:
            for sc in replay(by_id, ids, ch, flags, vars_, aid.get(area, -1)):
                sc['area'] = area
                out.append(sc)
    return out, flags, vars_


def area_flag_sets(by_id, ids, packs):
    """{area: flags that scripts in the area's own packs switch on (constant 1, never 0)}. Chapter control scripts
    and flags 2000+ (debug / save-code flags) are left out."""
    out = {}
    for area in AREAS:
        vals = {}
        for name, pl in packs.items():
            if re.match(r'bhb_ai_s\d\d_HE$', name) or not any(p.startswith('area/%s/' % area) for p in pl):
                continue
            sc = by_id.get(ids.get(name))
            if sc is None:
                continue
            for args in sc.const_calls(C_FLAG_SET):
                if len(args) == 2 and isinstance(args[0], int) and isinstance(args[1], int):
                    vals.setdefault(args[0], set()).add(1 if args[1] else 0)
        out[area] = {f for f, v in vals.items() if v == {1} and f not in DEBUG_FLAGS}
    return out


def sections(area):
    """{section name ('area/hon_under_B'): (minx, maxx, minz, maxz)} from every positioned event of each section."""
    folder = os.path.join(ROM, 'area', area)
    out = {}
    for f in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if not f.endswith('.evd') or f[:-4] == area or not re.match(re.escape(area) + r'_[A-Z]+\.evd$', f):
            continue
        xs, zs = [], []
        for ev in evd_dump.read_evd(os.path.join(folder, f)):
            if evd_dump.get(ev, 'TYPE') not in ('EV_TYPE_CELL', 'EV_TYPE_PORTAL', 'EV_TYPE_OBJ'):
                continue
            x, z = evd_dump.get(ev, 'POSITION_X'), evd_dump.get(ev, 'POSITION_Z')
            if isinstance(x, float) and isinstance(z, float):
                xs.append(x)
                zs.append(z)
        if xs:
            out['area/' + f[:-4]] = (min(xs), max(xs), min(zs), max(zs))
    return out


def section_at(area, x, z):
    best = None
    for name, (x0, x1, z0, z1) in sections(area).items():
        dx = max(x0 - x, 0, x - x1)
        dz = max(z0 - z, 0, z - z1)
        key = (math.hypot(dx, dz), (x1 - x0) * (z1 - z0))
        if best is None or key < best[0]:
            best = (key, name)
    return best[1] if best else 'area/%s_A' % area


def boxes(area):
    """{NAME_ID: (x, y, z, sx, sy, sz)} for every trigger box (EV_TYPE_AREAHIT) of an area."""
    folder = os.path.join(ROM, 'area', area)
    out = {}
    for f in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if f.endswith('.evd'):
            for ev in evd_dump.read_evd(os.path.join(folder, f)):
                if evd_dump.get(ev, 'TYPE') == 'EV_TYPE_AREAHIT':
                    v = [evd_dump.get(ev, k) for k in ('POSITION_X', 'POSITION_Y', 'POSITION_Z', 'SCALE_X',
                                                         'SCALE_Y', 'SCALE_Z')]
                    if all(isinstance(a, float) for a in v):
                        for key in (evd_dump.get(ev, 'NAME_ID'), evd_dump.get(ev, 'NAME')):
                            if key:
                                out.setdefault(key, tuple(v))
    return out


def hero_script(area, ids, packs):
    """The AI script Derrick should run in an area: its chapter control script bhb_ai_sNN_HE (the lowest chapter
    in the area's own pack), else the area's earliest scene script ending in _HE (labo1, labo2, opening, ending).
    Derrick's saved block keeps this id at +0x2E; a script that is not in the new area's packs leaves him out."""
    mine = sorted(n for n, pl in packs.items() if n.endswith('_HE') and 'area/%s/%s.pak' % (area, area) in pl)
    chapter = [n for n in mine if re.match(r'bhb_ai_s\d\d_HE$', n)]
    own = [n for n in mine if len(packs[n]) == 1]      # scene scripts only this area carries (ending also has s09e01)
    return (chapter or own or mine or [''])[0]


def checkpoint_progress():
    """{checkpoint: entry} for all 192 checkpoints. See docs/formats/save_story.md for how each field is found."""
    import save_checkpoint
    by_id, ids, packs = load_scripts()
    scenes, _f, _v = story(by_id, ids, packs)
    per_area = area_flag_sets(by_id, ids, packs)
    rows = save_checkpoint.checkpoints()
    # flags on when each area starts: everything the earlier areas switch on, plus the replayed scenes' flags
    start, acc = {}, set()
    for area in AREAS:
        start[area] = set(acc)
        acc |= per_area[area]
        for sc in scenes:
            if sc['area'] == area:
                acc |= set(sc['flags_before']) | set(sc['sets'])
    out = {}
    lower = {}                   # (area, chapter) -> earliest scene index allowed (chapters run in story order)
    order = sorted(range(len(rows)), key=lambda i: (AREAS.index(rows[i][1]) if rows[i][1] in AREAS else 99,
                                                     rows[i][3], i))
    for i in order:
        name, area, place, chapter = rows[i]
        bx = boxes(area)
        e = {'index': i, 'area': area, 'chapter': chapter}
        box = bx.get('areahit_' + name[len('save_'):]) if name.startswith('save_') else None
        mine = [sc for sc in scenes if sc['area'] == area]
        lb = max([v for (a, c), v in lower.items() if a == area and c < chapter] or [0])
        pending = None
        if name.startswith('save_cpai'):
            pending = next((sc for sc in mine if name in sc['cpai']), None)
            if pending and pending['trigger'] in bx and box is None:
                box = bx[pending['trigger']]
        if box is None and name == area and mine:
            pending = mine[0]
            box = bx.get(pending['trigger'])
        if box is not None:
            x, y, z, sx, sy, sz = box
            e['position'] = [round(x, 2), round(y - sy + 1.5, 2), round(z, 2)]
            e['section'] = section_at(area, x, z)
            if pending is None and mine:
                scored = sorted((math.hypot(bx[sc['trigger']][0] - x, bx[sc['trigger']][2] - z), k)
                                for k, sc in enumerate(mine) if sc['trigger'] in bx and k >= lb)
                if scored:
                    # a save box usually sits just after one scene and before the next: on a near tie take the later
                    near = [k for d, k in scored if d <= scored[0][0] * 1.25]
                    last = mine[max(near)]['trigger']
                    pending = mine[min(k for k in near if mine[k]['trigger'] == last)]
        if pending is not None:
            k = mine.index(pending)
            lower[(area, chapter)] = max(lower.get((area, chapter), 0), k)
            block = next((sc['block'] for sc in reversed(mine[:k + 1]) if sc['block']), '')
            e.update(method='scene', scene=pending['state'], block=block,
                     flags=sorted(set(start[area]) | set(pending['flags_before'])))
        else:
            e.update(method='area', flags=sorted(start[area]))
        hero = pending['chapter'] if pending is not None else hero_script(area, ids, packs)
        e.update(hero_ai=ids.get(hero, 0), hero_ai_name=hero)
        out[name] = e
    out = dict(sorted(out.items(), key=lambda kv: kv[1]['index']))
    return out


def main():
    by_id, ids, packs = load_scripts()
    if '--json' in sys.argv:
        dst = sys.argv[sys.argv.index('--json') + 1]
        data = checkpoint_progress()
        about = ('Story flags a save must hold for each checkpoint; made by tools/story_flags.py. '
                 'See docs/formats/save_story.md.')
        nl = chr(10)
        with open(dst, 'w', newline=nl) as f:     # one checkpoint per line
            f.write('{"about": %s,%s "checkpoints": {%s' % (json.dumps(about), nl, nl))
            f.write((',' + nl).join('  %s: %s' % (json.dumps(k), json.dumps(v)) for k, v in data.items()))
            f.write(nl + ' }' + nl + '}' + nl)
        print('wrote %s (%d checkpoints)' % (dst, len(data)))
        return 0
    scenes, flags, vars_ = story(by_id, ids, packs)
    for sc in scenes:
        print('%-9s %-14s %-22s block=%-44s trig=%-36s sets=%s%s' % (
            sc['area'], sc['chapter'][7:], sc['state'], sc['block'], sc['trigger'], sc['sets'],
            ' cpai=%s' % sc['cpai'] if sc['cpai'] else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
