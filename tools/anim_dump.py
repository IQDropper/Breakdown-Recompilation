"""anim_dump.py - read Breakdown's skeletons and animations (pak kind 0, magic-less 'GYM' data).

Format notes: docs/formats/skeleton_animation.md. In short:
  * Every animation is a list of TRACKS. A track is one node of a transform chain: a translation (type 0xB),
    a rotation (type 7 = 3 x int16 Euler angles, 32768 = 180 deg), a scale, or a single axis of one of those.
  * The node tree is the CHAIN TABLE: lists of track numbers from the root down, -1 between lists, -2 at the end.
  * A rotation track has no length of its own. The bone length (offset from the parent) comes from a second,
    "initial" animation whose tracks are all translations: gym_INITIAL_TRANS_A1 (id 1101) for the 24-track body.
    gym_INITIAL_ROT_A (id 1100) holds the rest-pose rotations the models are built in.
  * Keys are whole poses ("frames"); one key every N game frames (N = top 4 bits of header +0x0C).

Usage:
  python tools/anim_dump.py <pak> <anim id>                      header, tracks, chains, first/last key
  python tools/anim_dump.py <pak> <anim id> --keys               every key (decoded values)
  python tools/anim_dump.py <pak> <anim id> --json out.json      everything as JSON (keys sampled per frame)
  python tools/anim_dump.py <pak> --skeleton [trans id] [rot id] skeleton from the INITIAL anims (default 1101/1100)
  python tools/anim_dump.py <romdata> --find <id>                which pak holds an anim id
  python tools/anim_dump.py <romdata> --check                    parse every animation in every pak, report failures
"""
import bisect
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bdpk  # noqa: E402

# track types (byte +4 of a track record) -> (name, data size in bytes). Sizes match the game's table at 0x27C580.
TYPES = {
    0x0: ('scale_x', 4), 0x1: ('scale_y', 4), 0x2: ('scale_z', 4), 0x3: ('scale_xyz', 12),
    0x4: ('rot_x', 2), 0x5: ('rot_y', 2), 0x6: ('rot_z', 2), 0x7: ('rot_xyz', 6),
    0x8: ('trans_x', 4), 0x9: ('trans_y', 4), 0xA: ('trans_z', 4), 0xB: ('trans_xyz', 12),
    # only in camera animations ('..._Camera_..', '_CM_'); the body pose code ignores them
    0xC: ('vec3_c', 12), 0xD: ('vec3_d', 12), 0xE: ('vec3_e', 12), 0xF: ('cam_float', 4),
}
ANGLE = math.pi / 32768.0          # int16 angle unit (game constant at 0x20C630, a double)

# header +0 flag bits
F_SKIP = 0x2      # tracks whose record has flags bit 2 have no data at all (use the rest pose)
F_DELTA = 0x4     # key 0 is full; later keys leave out constant tracks (record flags bit 4) -> smaller stride
F_BLOCK = 0x8     # keys stored per track in blocks of 30 ('BLOCK00' table at the start of the key data)
F_CURVE = 0x100   # hermite curves (code at 0x193740); no file in the game uses it


def pak_entry(pak, anim_id, kind=0):
    """(bytes, name) of one entry; data ends at the next entry (pak 'flag 1' sizes are in-memory sizes)."""
    d = open(pak, 'rb').read()
    es = bdpk.entries(pak)
    offs = sorted(e[3] for e in es) + [len(d)]
    for k, i, sz, off, name in es:
        if k == kind and i == anim_id:
            return d[off:offs[bisect.bisect_right(offs, off)]], name
    raise KeyError('id %d kind %d not in %s' % (anim_id, kind, pak))


def find_pak(root, anim_id, kind=0):
    """First pak under root holding the id (common/base.pak first, as the game loads it at boot)."""
    paks = []
    for r, _, fs in os.walk(root):
        paks += [os.path.join(r, f) for f in fs if f.endswith('.pak')]
    paks.sort(key=lambda p: (not p.replace('\\', '/').endswith('common/base.pak'), p))
    for p in paks:
        if any(k == kind and i == anim_id for k, i, *_ in bdpk.entries(p)):
            return p
    return None


class Anim:
    """One animation. Mirrors the game's 5-pointer handle built by 0x192C10."""

    def __init__(self, data, name='', anim_id=-1):
        b = self.b = data
        self.name, self.id = name, anim_id
        (self.flags, self.ntracks, self.nframes, self.delta_size) = struct.unpack_from('<4H', b, 0)
        (self.unk_f8,) = struct.unpack_from('<f', b, 8)
        (w0c,) = struct.unpack_from('<I', b, 0x0C)
        self.step = w0c >> 28                      # one key every `step` frames (0 or 1 = every frame)
        self.packed_rot = (w0c & 0xF0000000) > 0x50000000   # 4-byte packed rotations (never used in the game)
        (self.unk10, self.work_size, self.unpacked_size, self.chain_len,
         self.o_tracks, self.o_chains, self.o_keys, self.key_size) = struct.unpack_from('<8I', b, 0x10)
        self.tracks = []
        for t in range(self.ntracks):
            parent, link, typ, has_child, joint, tfl, off_delta, off_full = struct.unpack_from(
                '<hhBBBBII', b, self.o_tracks + 16 * t)
            self.tracks.append(dict(index=t, type=typ, type_name=TYPES.get(typ, ('?', 0))[0], parent=parent,
                                    link=link, has_child=has_child, joint=joint, flags=tfl,
                                    off_delta=off_delta, off_full=off_full))
        self.chains = self._chains()
        self.parents = self._parents()
        n = self.nframes
        s = self.step if self.step > 1 else 1
        self.nkeys = (n - 1) // s + 1 + (1 if s > 1 and (n - 1) % s else 0) if n else 0
        self.block = None
        if self.flags & F_BLOCK:
            magic = b[self.o_keys:self.o_keys + 8]
            nb_per_track, block_len = struct.unpack_from('<2I', b, self.o_keys + 0x0C)
            self.block = dict(magic=magic.rstrip(b'\0').decode('latin-1'), blocks=nb_per_track, length=block_len,
                              unk08=struct.unpack_from('<I', b, self.o_keys + 8)[0])

    # ---- structure -------------------------------------------------------------------------------------------
    def _chains(self):
        out, cur, p = [], [], self.o_chains
        for _ in range(self.chain_len):
            (v,) = struct.unpack_from('<h', self.b, p)
            p += 2
            if v == -2:
                break
            if v == -1:
                out.append(cur)
                cur = []
            else:
                cur.append(v)
        if cur:
            out.append(cur)
        return out

    def _parents(self):
        """parent track per track, from the chain table (what the game walks in 0x194040)."""
        par = [-1] * self.ntracks
        for ch in self.chains:
            for a, c in zip(ch, ch[1:]):
                par[c] = a
        return par

    # ---- key data --------------------------------------------------------------------------------------------
    def key_frame(self, k):
        """game frame of key k"""
        s = self.step if self.step > 1 else 1
        return min(k * s, self.nframes - 1)

    def _addr(self, key, t):
        """byte offset of track t's value in key `key` (0x192CA0 / 0x192D40), or None if the track has no data."""
        tr = self.tracks[t]
        if self.flags & F_SKIP and tr['flags'] & 2:
            return None
        base = self.o_keys
        if self.flags & F_BLOCK:
            bl = self.block
            (v,) = struct.unpack_from('<I', self.b, base + 0x40 + (bl['blocks'] * t + key // bl['length']) * 4)
            off = base + (v & 0xFFFFFF)
            if v >> 24:                                   # animated inside this block: one value per key
                off += TYPES[tr['type']][1] * (key % bl['length'])
            return off
        if self.flags & F_DELTA and key != 0:
            if tr['flags'] & 4:                           # constant track: only stored in key 0
                return base + tr['off_full']
            return base + self.key_size + self.delta_size * (key - 1) + tr['off_delta']
        return base + self.key_size * key + tr['off_full']

    def value(self, key, t):
        """decoded value of track t at key: tuple of floats (translation/scale) or int16 angles; None = no data"""
        a = self._addr(key, t)
        if a is None:
            return None
        typ = self.tracks[t]['type']
        if typ in (0, 1, 2, 8, 9, 10, 0xF):
            return struct.unpack_from('<f', self.b, a)
        if typ in (3, 0xB, 0xC, 0xD, 0xE):
            return struct.unpack_from('<3f', self.b, a)
        if typ in (4, 5, 6):
            return struct.unpack_from('<h', self.b, a)
        if typ == 7:
            if self.packed_rot:                           # 0x192B90: 11/11/10-bit packing
                (v,) = struct.unpack_from('<I', self.b, a)
                xs = ((v >> 21) & 0x7FF) << 5
                ys = ((v >> 10) & 0x7FF) << 5
                zs = (v & 0x3FF) << 6
                return tuple(x - 0x10000 if x >= 0x8000 else x for x in (xs, ys, zs))
            return struct.unpack_from('<3h', self.b, a)
        raise ValueError('track type %#x' % typ)

    def pose(self, key):
        """[{'t':(x,y,z) or None, 'r':(rx,ry,rz) int16 or None, 's':(sx,sy,sz) or None}] per track (the game's
        32-byte pose record: translate, scale, 3 x int16 rotation)."""
        out = []
        for t, tr in enumerate(self.tracks):
            v = self.value(key, t)
            p = {'t': None, 'r': None, 's': None}
            typ = tr['type']
            if v is not None:
                if typ == 0xB:
                    p['t'] = v
                elif typ in (8, 9, 10):
                    p['t'] = tuple(v[0] if i == typ - 8 else 0.0 for i in range(3))
                elif typ == 7:
                    p['r'] = v
                elif typ in (4, 5, 6):
                    p['r'] = tuple(v[0] if i == typ - 4 else 0 for i in range(3))
                elif typ == 3:
                    p['s'] = v
                elif typ in (0, 1, 2):
                    p['s'] = tuple(v[0] if i == typ else 1.0 for i in range(3))
            out.append(p)
        return out

    def summary(self):
        return dict(id=self.id, name=self.name, flags=self.flags, tracks=self.ntracks, frames=self.nframes,
                    step=self.step, keys=self.nkeys, key_size=self.key_size, delta_size=self.delta_size,
                    unknown_float_08=round(self.unk_f8, 4), work_size=self.work_size,
                    unpacked_size=self.unpacked_size, packed_rotations=self.packed_rot, block=self.block,
                    offsets=dict(tracks=self.o_tracks, chains=self.o_chains, keys=self.o_keys),
                    size=len(self.b))


# ---- skeleton / matrices ---------------------------------------------------------------------------------------
def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def mat_trs(t, r, s=None):
    """local matrix of one track, column-vector form: T * Rz * Ry * Rx * S  (the game builds the row-vector
    equivalent: translate, then rotZ, rotY, rotX on its matrix stack, 0x194040)."""
    rx, ry, rz = (a * ANGLE for a in (r or (0, 0, 0)))
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    Rx = [[1, 0, 0, 0], [0, cx, -sx, 0], [0, sx, cx, 0], [0, 0, 0, 1]]
    Ry = [[cy, 0, sy, 0], [0, 1, 0, 0], [-sy, 0, cy, 0], [0, 0, 0, 1]]
    Rz = [[cz, -sz, 0, 0], [sz, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    m = mat_mul(Rz, mat_mul(Ry, Rx))
    if s:
        m = mat_mul(m, [[s[0], 0, 0, 0], [0, s[1], 0, 0], [0, 0, s[2], 0], [0, 0, 0, 1]])
    tx, ty, tz = t or (0.0, 0.0, 0.0)
    m[0][3], m[1][3], m[2][3] = tx, ty, tz
    return m


def resolve(anim, key, ref=None):
    """local (t, r, s) per track the way the game fills its pose (0x1933C0): a rotation track takes its
    translation from the reference (INITIAL_TRANS) animation's track of the same number."""
    pose = anim.pose(key)
    refpose = ref.pose(0) if ref else None
    out = []
    for t, p in enumerate(pose):
        tr = p['t']
        if anim.tracks[t]['type'] == 7 and refpose and t < len(refpose):
            tr = refpose[t]['t']
        out.append((tr, p['r'], p['s']))
    return out


def world(anim, local):
    """world matrix per track, walking the chain table like 0x194040"""
    ident = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    W = [None] * anim.ntracks
    for ch in anim.chains:
        m = ident
        for t in ch:
            m = mat_mul(m, mat_trs(*local[t]))
            W[t] = m
    return W


def skeleton(trans, rot=None):
    """bones of a skeleton: one per track; rest offset from INITIAL_TRANS, rest rotation from INITIAL_ROT."""
    loc = []
    tp = trans.pose(0)
    rp = rot.pose(0) if rot else [None] * trans.ntracks
    for t in range(trans.ntracks):
        loc.append((tp[t]['t'], rp[t]['r'] if rp[t] else None, None))
    W = world(trans, loc)
    bones = []
    for t, tr in enumerate(trans.tracks):
        bones.append(dict(track=t, joint=tr['joint'], parent=trans.parents[t], name=BODY24.get(t, '') if
                          trans.ntracks == 24 else '', offset=[round(v, 4) for v in loc[t][0]],
                          rest_rot=list(loc[t][1]) if loc[t][1] else None,
                          rest_rot_deg=[round(a * ANGLE * 180 / math.pi, 2) for a in loc[t][1]] if loc[t][1] else None,
                          world_pos=[round(W[t][i][3], 4) for i in range(3)] if W[t] else None))
    return bones


# names for the 24-track humanoid body (from positions in INITIAL_TRANS_A1 and the chain table; our names)
BODY24 = {0: 'root_move', 1: 'hips', 2: 'spine', 3: 'chest', 4: 'neck', 5: 'head',
          6: 'clavicle_L', 7: 'upperarm_L', 8: 'forearm_L', 9: 'hand_L',
          10: 'clavicle_R', 11: 'upperarm_R', 12: 'forearm_R', 13: 'hand_R',
          14: 'pelvis', 15: 'thigh_L', 16: 'calf_L', 17: 'foot_L', 18: 'toe_L',
          19: 'thigh_R', 20: 'calf_R', 21: 'foot_R', 22: 'toe_R', 23: 'extra'}


# ---- command line ----------------------------------------------------------------------------------------------
def fmt_val(v):
    if v is None:
        return '-'
    return ' '.join(('%.4f' % x) if isinstance(x, float) else ('%d' % x) for x in v)


def dump_text(a, all_keys=False):
    s = a.summary()
    print('anim %d %s' % (a.id, a.name))
    print('  flags=%#x tracks=%d frames=%d step=%d keys=%d key_size=%d delta_size=%d size=%d' % (
        a.flags, a.ntracks, a.nframes, a.step, a.nkeys, a.key_size, a.delta_size, s['size']))
    print('  +08 float=%.3f  work_size=%#x  unpacked_size=%#x  chain_len=%d  block=%s' % (
        a.unk_f8, a.work_size, a.unpacked_size, a.chain_len, a.block))
    print('  chains: %s' % ' | '.join(' '.join(map(str, c)) for c in a.chains))
    print('  trk type      parent joint flags  name')
    for tr in a.tracks:
        print('  %3d %-9s %6d %5d %5x  %s' % (tr['index'], tr['type_name'], a.parents[tr['index']], tr['joint'],
                                           tr['flags'], BODY24.get(tr['index'], '') if a.ntracks == 24 else ''))
    keys = range(a.nkeys) if all_keys else sorted({0, a.nkeys - 1})
    for k in keys:
        print('  key %d (frame %d):' % (k, a.key_frame(k)))
        for t in range(a.ntracks):
            print('    %3d %s' % (t, fmt_val(a.value(k, t))))


def to_json(a, ref=None):
    keys = []
    for k in range(a.nkeys):
        keys.append(dict(key=k, frame=a.key_frame(k), values=[a.value(k, t) for t in range(a.ntracks)]))
    out = dict(a.summary(), chains=a.chains, parents=a.parents, tracks=a.tracks, keys=keys)
    if ref:
        out['reference'] = dict(id=ref.id, name=ref.name)
    return out


def load(path, anim_id):
    if os.path.isdir(path):
        p = find_pak(path, anim_id)
        if not p:
            raise KeyError('anim %d not found under %s' % (anim_id, path))
        path = p
    data, name = pak_entry(path, anim_id)
    return Anim(data, name, anim_id)


def check_all(root):
    ok = bad = 0
    seen = set()
    fails = []
    for r, _, fs in os.walk(root):
        for f in sorted(fs):
            if not f.endswith('.pak'):
                continue
            p = os.path.join(r, f)
            d = open(p, 'rb').read()
            es = bdpk.entries(p)
            offs = sorted(e[3] for e in es) + [len(d)]
            for k, i, sz, off, name in es:
                if k or (i, name) in seen:
                    continue
                seen.add((i, name))
                try:
                    a = Anim(d[off:offs[bisect.bisect_right(offs, off)]], name, i)
                    assert a.flags & ~0xE == 0, 'flags %#x' % a.flags
                    for kk in range(a.nkeys):
                        for t in range(a.ntracks):
                            ad = a._addr(kk, t)
                            if ad is not None and ad + TYPES[a.tracks[t]['type']][1] > len(a.b):
                                raise ValueError('key %d track %d past end' % (kk, t))
                            a.value(kk, t)
                    ok += 1
                except Exception as e:  # noqa: BLE001 - report every failure
                    bad += 1
                    fails.append('%d %s (%s): %s' % (i, name, f, e))
    print('animations parsed: %d ok, %d failed' % (ok, bad))
    for x in fails[:40]:
        print('  ' + x)
    return 1 if bad else 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    path = argv[0]
    if argv[1] == '--check':
        return check_all(path)
    if argv[1] == '--find':
        print(find_pak(path, int(argv[2])))
        return 0
    if argv[1] == '--skeleton':
        ids = [int(x) for x in argv[2:4]] or [1101, 1100]
        tr = load(path, ids[0])
        rot = load(path, ids[1]) if len(ids) > 1 else None
        print('skeleton from %d %s + %s' % (tr.id, tr.name, '%d %s' % (rot.id, rot.name) if rot else 'no rest rotation'))
        print(' trk joint parent name         offset (from parent)          rest rot (deg)            world position')
        for bn in skeleton(tr, rot):
            print(' %3d %5d %6d %-12s %-29s %-25s %s' % (bn['track'], bn['joint'], bn['parent'], bn['name'],
                                                         bn['offset'], bn['rest_rot_deg'], bn['world_pos']))
        return 0
    a = load(path, int(argv[1]))
    if '--json' in argv:
        out = argv[argv.index('--json') + 1]
        with open(out, 'w') as f:
            json.dump(to_json(a), f, indent=1)
        print('wrote', out)
        return 0
    dump_text(a, '--keys' in argv)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
