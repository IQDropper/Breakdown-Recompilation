"""anim_gltf.py - export one skinned Breakdown model + its skeleton + one animation to a .glb (glTF 2.0).

  python tools/anim_gltf.py <romdata> <model id> <anim id> out.glb [--trans 1101] [--rot 1100] [--fps 60]

  Derrick (third person body):  python tools/anim_gltf.py game_files/romdata 8 1794 derrick_riderkick.glb

What it does (details in docs/formats/skeleton_animation.md):
  * skeleton = one glTF node per track; offsets from INITIAL_TRANS (default 1101), rest rotations from
    INITIAL_ROT (default 1100). The game stacks the root track (0) on top of the hips offset (track 1), which
    puts its joints 10.35 units above the mesh; skinning cancels that out. Here the hips offset is reduced by the
    rest root offset so the bones sit inside the mesh; the result on screen is the same.
  * model = NUDX parts; skinned vertices (formats 0x0D/0x0E/0x0F) carry 4 x int16 weights (32767 = 1.0) and
    4 x u16 indices into the part's bone list (16 bytes at part header +0x90). A bone list value is a JOINT number
    (track record byte +6); the matrix is the track with that joint number (joint 0 = track 1, the hips).
  * animation = one key every `step` frames; rotations go out as quaternions (Rz*Ry*Rx), linear interpolation.
  * a parent node scales by 0.1 (10 units = 1 m, Derrick is 16.6 units tall) and mirrors Z (Xbox Direct3D is
    left-handed, glTF is right-handed).
No textures yet (plain grey); model geometry reader kept minimal (no LOD/material handling).
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_dump as A  # noqa: E402

VSIZE = {0x04: 20, 0x05: 28, 0x06: 24, 0x07: 32, 0x0D: 44, 0x0E: 40, 0x0F: 48, 0x16: 32, 0x17: 40}
SKINNED = (0x0D, 0x0E, 0x0F)


# ---- NUDX (minimal) --------------------------------------------------------------------------------------------
def nud_parts(b):
    if b[:4] != b'NUDX':
        raise ValueError('not a NUDX model')
    (nparts,) = struct.unpack_from('<H', b, 0xA)
    out = []
    for i in range(nparts):
        o = 0x20 + 0xA0 * i
        off, size, _u, fl = struct.unpack_from('<4I', b, o)
        pal = [x for x in b[o + 0x90:o + 0xA0] if x != 0xFF]
        out.append(dict(off=off, size=size, flags=fl, palette=pal, meshes=_meshes(b, off, min(off + size, len(b)))))
    return out


def _meshes(b, start, end):
    res, o, cur = [], start, None
    while o < end - 4:
        fmt, hi = b[o], b[o + 1]
        if hi == 0x20 and fmt in VSIZE and b[o + 2:o + 4] == b'\0\0':
            (n,) = struct.unpack_from('<I', b, o + 12)
            cur = dict(fmt=fmt, n=n, vs=VSIZE[fmt], voff=o + 0x20, tris=[])
            res.append(cur)
            o += 0x20 + n * VSIZE[fmt]
            continue
        if struct.unpack_from('<H', b, o)[0] == 0x3001 and cur is not None:
            o = _pushbuf(b, o + 0x30, cur, end)
            continue
        o += 4
    return res


def _pushbuf(b, p, cur, end):
    """NV2A push buffer: BEGIN_END (0x17FC), ARRAY_ELEMENT16 (0x1800), ARRAY_ELEMENT32 (0x1808), DRAW_ARRAYS (0x1810)"""
    prim, idx = None, []

    def flush():
        if prim == 6:                      # triangle strip
            for i in range(len(idx) - 2):
                a, c, d = idx[i:i + 3]
                if a != c and c != d and a != d:
                    cur['tris'].append((a, c, d) if i % 2 == 0 else (c, a, d))
        elif prim == 5:                    # triangle list
            for i in range(0, len(idx) - 2, 3):
                cur['tris'].append(tuple(idx[i:i + 3]))
    while p < end - 4:
        (h,) = struct.unpack_from('<I', b, p)
        p += 4
        meth, cnt = h & 0x1FFF, (h >> 18) & 0x7FF
        if meth == 0x17FC:
            (v,) = struct.unpack_from('<I', b, p)
            p += 4
            if v == 0:
                flush()
                return p
            prim, idx = v, []
        elif meth in (0x1800, 0x1808, 0x1810):
            for k in range(cnt):
                (v,) = struct.unpack_from('<I', b, p + 4 * k)
                if meth == 0x1800:
                    idx += [v & 0xFFFF, v >> 16]
                elif meth == 0x1808:
                    idx.append(v)
                else:
                    idx += list(range(v & 0xFFFFFF, (v & 0xFFFFFF) + (v >> 24) + 1))
            p += 4 * cnt
        else:
            return p - 4
    return p


# ---- maths -----------------------------------------------------------------------------------------------------
def quat_euler(r):
    """quaternion (x,y,z,w) of Rz*Ry*Rx from int16 angles"""
    hx, hy, hz = (a * A.ANGLE / 2 for a in r)
    qx = (math.sin(hx), 0, 0, math.cos(hx))
    qy = (0, math.sin(hy), 0, math.cos(hy))
    qz = (0, 0, math.sin(hz), math.cos(hz))
    return qmul(qz, qmul(qy, qx))


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def inv4(m):
    """inverse of a rigid 4x4 (rotation + translation) in row-major lists"""
    r = [[m[j][i] for j in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0, 0, 0, 1]]


# ---- export ----------------------------------------------------------------------------------------------------
def build(romdata, model_id, anim_id, trans_id=1101, rot_id=1100, fps=60.0):
    mpak = A.find_pak(romdata, model_id, kind=1)
    mdata, mname = A.pak_entry(mpak, model_id, kind=1)
    trans, rot, anim = A.load(romdata, trans_id), A.load(romdata, rot_id), A.load(romdata, anim_id)
    if not (trans.ntracks == rot.ntracks == anim.ntracks):
        raise ValueError('track counts differ: %d/%d/%d' % (trans.ntracks, rot.ntracks, anim.ntracks))
    n = anim.ntracks
    joint_to_track = {}
    for tr in anim.tracks:
        if tr['type'] == 7:
            joint_to_track.setdefault(tr['joint'], tr['index'])
    root_rest = trans.pose(0)[0]['t']
    rest_t, rest_r = [], []
    for t in range(n):
        tt = list(trans.pose(0)[t]['t'])
        if t == 1 or (anim.parents[t] == 0 and anim.tracks[t]['type'] == 7):
            tt = [a - b for a, b in zip(tt, root_rest)]   # undo the double root offset (see docstring)
        rest_t.append(tt)
        rest_r.append(rot.pose(0)[t]['r'] or (0, 0, 0))
    W = A.world(anim, [(rest_t[t], rest_r[t], None) for t in range(n)])

    # geometry
    pos, nrm, jnt, wgt, ind = [], [], [], [], []
    for p in nud_parts(mdata):
        for m in p['meshes']:
            base = len(pos) // 3
            for i in range(m['n']):
                o = m['voff'] + i * m['vs']
                pos += struct.unpack_from('<3f', mdata, o)
                if m['fmt'] in SKINNED and p['palette']:
                    w = struct.unpack_from('<4h', mdata, o + m['vs'] - 16)
                    ix = struct.unpack_from('<4H', mdata, o + m['vs'] - 8)
                    js = [joint_to_track.get(p['palette'][k] if k < len(p['palette']) else 0, 1) for k in ix]
                    ws = [max(0, x) / 32767.0 for x in w]
                else:
                    js, ws = [1, 0, 0, 0], [1.0, 0, 0, 0]
                s = sum(ws) or 1.0
                jnt += js
                wgt += [x / s for x in ws]
            for tri in m['tris']:
                if max(tri) < m['n']:
                    ind += [base + v for v in tri]

    # animation samples
    times, trans_keys, rot_keys = [], [], [[] for _ in range(n)]
    prev = [None] * n
    for k in range(anim.nkeys):
        times.append(anim.key_frame(k) / fps)
        pose = anim.pose(k)
        trans_keys += list(pose[0]['t'] or root_rest)
        for t in range(n):
            q = quat_euler(pose[t]['r'] or rest_r[t]) if anim.tracks[t]['type'] in (4, 5, 6, 7) else \
                quat_euler(rest_r[t])
            if prev[t] and sum(a * b for a, b in zip(q, prev[t])) < 0:
                q = tuple(-x for x in q)
            prev[t] = q
            rot_keys[t] += q

    # buffers
    blob = bytearray()
    views, accs = [], []

    def add(data, fmt, comp, typ, count, target=None, minmax=False):
        while len(blob) % 4:
            blob.append(0)
        raw = struct.pack('<%d%s' % (len(data), fmt), *data)
        v = dict(buffer=0, byteOffset=len(blob), byteLength=len(raw))
        if target:
            v['target'] = target
        views.append(v)
        blob.extend(raw)
        a = dict(bufferView=len(views) - 1, componentType=comp, count=count, type=typ)
        if minmax:
            w = {'SCALAR': 1, 'VEC3': 3}[typ]
            a['min'] = [min(data[i::w]) for i in range(w)]
            a['max'] = [max(data[i::w]) for i in range(w)]
        accs.append(a)
        return len(accs) - 1

    nv = len(pos) // 3
    a_pos = add(pos, 'f', 5126, 'VEC3', nv, 34962, True)
    a_jnt = add(jnt, 'H', 5123, 'VEC4', nv, 34962)
    a_wgt = add(wgt, 'f', 5126, 'VEC4', nv, 34962)
    a_ind = add(ind, 'I', 5125, 'SCALAR', len(ind), 34963)
    ibm = []
    for t in range(n):
        mi = inv4(W[t])
        ibm += [mi[r][c] for c in range(4) for r in range(4)]     # column-major
    a_ibm = add(ibm, 'f', 5126, 'MAT4', n)
    a_time = add(times, 'f', 5126, 'SCALAR', len(times), None, True)
    a_tr = add(trans_keys, 'f', 5126, 'VEC3', len(times))
    a_rot = [add(rot_keys[t], 'f', 5126, 'VEC4', len(times)) for t in range(n)]

    names = A.BODY24 if n == 24 else {}
    nodes = []
    for t in range(n):
        nd = dict(name='%02d_%s' % (t, names.get(t, 'track')), translation=rest_t[t],
                  rotation=list(quat_euler(rest_r[t])))
        kids = [c for c in range(n) if anim.parents[c] == t]
        if kids:
            nd['children'] = kids
        nodes.append(nd)
    roots = [t for t in range(n) if anim.parents[t] == -1]
    nodes.append(dict(name='breakdown_space', scale=[0.1, 0.1, -0.1], children=roots))          # n
    nodes.append(dict(name=mname, mesh=0, skin=0))                                              # n+1
    samplers = [dict(input=a_time, output=a_tr, interpolation='LINEAR')]
    channels = [dict(sampler=0, target=dict(node=0, path='translation'))]
    for t in range(n):
        samplers.append(dict(input=a_time, output=a_rot[t], interpolation='LINEAR'))
        channels.append(dict(sampler=len(samplers) - 1, target=dict(node=t, path='rotation')))
    gl = dict(
        asset=dict(version='2.0', generator='Breakdown-Recompilation tools/anim_gltf.py'),
        scene=0, scenes=[dict(nodes=[n, n + 1])], nodes=nodes,
        meshes=[dict(name=mname, primitives=[dict(attributes=dict(POSITION=a_pos, JOINTS_0=a_jnt, WEIGHTS_0=a_wgt),
                                                  indices=a_ind, material=0)])],
        materials=[dict(name='grey', doubleSided=True,
                        pbrMetallicRoughness=dict(baseColorFactor=[0.7, 0.7, 0.72, 1], metallicFactor=0,
                                                  roughnessFactor=0.8))],
        skins=[dict(joints=list(range(n)), inverseBindMatrices=a_ibm, skeleton=n)],
        animations=[dict(name=anim.name, samplers=samplers, channels=channels)],
        accessors=accs, bufferViews=views, buffers=[dict(byteLength=len(blob))])
    info = dict(model=mname, anim=anim.name, vertices=nv, triangles=len(ind) // 3, keys=anim.nkeys,
                seconds=round(times[-1], 3))
    return gl, bytes(blob), info


def write_glb(path, gl, blob):
    js = json.dumps(gl, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    blob += b'\0' * (-len(blob) % 4)
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total))
        f.write(struct.pack('<I4s', len(js), b'JSON') + js)
        f.write(struct.pack('<I4s', len(blob), b'BIN\0') + blob)


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    opt = lambda k, d: type(d)(argv[argv.index(k) + 1]) if k in argv else d  # noqa: E731
    gl, blob, info = build(argv[0], int(argv[1]), int(argv[2]), opt('--trans', 1101), opt('--rot', 1100),
                           opt('--fps', 60.0))
    write_glb(argv[3], gl, blob)
    print('wrote %s: %s' % (argv[3], info))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
