"""model_gltf.py - export Breakdown models to glTF 2.0 (.gltf + .bin + PNG textures next to it), for Blender.

  python tools/model_gltf.py <romdata> <id[,id...]> out.gltf [--pak PATH] [--lod N] [--anim ID] [--rest]
                             [--trans 1101] [--rot 1100] [--fps 60] [--attach ID:TRACK,...]
  python tools/model_gltf.py <romdata> --area hon_under_A out.gltf [--lod N] [--collision | --collision-only]

  Derrick:             python tools/model_gltf.py game_files/romdata 8 %TEMP%/derrick/derrick.gltf
  Derrick + rider kick python tools/model_gltf.py game_files/romdata 8 %TEMP%/derrick/kick.gltf --anim 1794
  Alex (body, head, hair, eyelashes):
                       python tools/model_gltf.py game_files/romdata 9,14,11,15 %TEMP%/alex/alex.gltf
  A whole section's scenery (7F Main Research Center, part A):
                       python tools/model_gltf.py game_files/romdata --area hon_under_A %TEMP%/hon_under_A/area.gltf

  Scenery + its collision as a see-through overlay (check collision against the visible rooms):
                       python tools/model_gltf.py game_files/romdata --area hon_under_A %TEMP%/hon_under_A/hit.gltf --collision

Several ids go into one scene (a character is a body + head + hair + ... model). Scenery ids repeat between
areas, so pass --pak for those (or use --area).

What comes out (layout: docs/formats/nud.md):
  * every part of the chosen LOD (default 0) as one glTF primitive: positions, normals, vertex colours,
    one or two UV sets; skinned parts also JOINTS_0 / WEIGHTS_0.
  * textures: the part's texture id (GIDX) is looked up in the game's own texture data (.nut / .pak TIM2,
    nearest file first) and written as PNG next to the .gltf, named like the texture dumps (<hash>_<w>x<h>_<fmt>),
    so an upscaled/replaced texture pack lines up by name.
  * materials: blend -> alphaMode BLEND, alpha test -> MASK, no culling -> doubleSided; the raw game values are
    kept in the material's "extras".
  * models with their own small rig (hair, Alex's face) are fixed whole to the head bone (names with hair /
    _head / matsuge); --attach 14:5 overrides. Hand models (_hand_l / _hand_r: arm pieces + palm + 15 finger
    joints) have each joint put on upper arm / forearm / hand by position; fingers stay in the rest pose.
    The hair, face and finger rigs themselves are not exported yet.
  * skinned models get the skeleton (rest offsets from INITIAL_TRANS 1101, rest turns from INITIAL_ROT 1100,
    see docs/formats/skeleton_animation.md); --anim adds one animation.
  * --collision adds the hit data (HD51, docs/formats/collision.md) as separate meshes named '<model> [hit]':
    semi-transparent, both sides, coloured by triangle type (solid = orange, type 2 = blue, 3 = green, 1 / 4 =
    purple, 15 = grey) and drawn on top of nothing else, so it can be checked against the scenery in any glTF
    viewer; the AEG edges come as red lines. --collision-only writes the collision alone. Every primitive keeps
    the surface numbers in "extras". For --area it takes the section's collision from the area pak
    (nud.section_collision); for model ids, each model's own hit blocks (in model space, like the model).
  * axes: 10 game units = 1 m, X mirrored (Xbox Direct3D is left-handed, glTF right-handed; mirroring X keeps
    the game's forward +Z as glTF's front). Baked into the data, so Blender gets plain unscaled objects.
Game files are read in place; output is game art: keep it out of git (write under %TEMP% or the kit's reports).
"""
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import anim_dump as A  # noqa: E402
import nud  # noqa: E402

S = 0.1                       # game units -> metres
C = (-S, S, S)                # point conversion: mirror X (keeps game forward +Z = glTF front)


def cpos(p):
    return (p[0] * C[0], p[1] * C[1], p[2] * C[2])


def cdir(n):
    x, y, z = -n[0], n[1], n[2]
    ln = math.sqrt(x * x + y * y + z * z) or 1.0
    return (x / ln, y / ln, z / ln)


def cquat(q):
    """rotation conjugated by the X mirror"""
    return (q[0], -q[1], -q[2], q[3])


def cmat(m):
    """4x4 row-major game matrix (column vectors) -> glTF space: C M C^-1"""
    d = (C[0], C[1], C[2], 1.0)
    return [[m[i][j] * d[i] / d[j] for j in range(4)] for i in range(4)]


def quat_euler(r):
    """quaternion (x,y,z,w) of Rz*Ry*Rx from int16 angles (game order, see skeleton_animation.md)"""
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


def inv_rigid(m):
    r = [[m[j][i] for j in range(3)] for i in range(3)]
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return [r[0] + [t[0]], r[1] + [t[1]], r[2] + [t[2]], [0, 0, 0, 1]]


# ---- glTF builder ----------------------------------------------------------------------------------------------
class Gltf:
    def __init__(self, out_path):
        self.path = out_path
        self.dir = os.path.dirname(os.path.abspath(out_path))
        self.stem = os.path.splitext(os.path.basename(out_path))[0]
        self.blob = bytearray()
        self.g = dict(asset=dict(version='2.0', generator='Breakdown-Recompilation tools/model_gltf.py'),
                      scene=0, scenes=[dict(nodes=[])], nodes=[], meshes=[], materials=[], textures=[],
                      images=[], samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
                      accessors=[], bufferViews=[], buffers=[])
        self.tex_by_name, self.mat_by_key = {}, {}
        self.notes = []

    def add(self, data, fmt, comp, typ, target=None, minmax=False, norm=False):
        while len(self.blob) % 4:
            self.blob.append(0)
        w = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[typ]
        flat = [x for v in data for x in v] if w > 1 and data and isinstance(data[0], (tuple, list)) else list(data)
        raw = struct.pack('<%d%s' % (len(flat), fmt), *flat)
        v = dict(buffer=0, byteOffset=len(self.blob), byteLength=len(raw))
        if target:
            v['target'] = target
        self.g['bufferViews'].append(v)
        self.blob.extend(raw)
        a = dict(bufferView=len(self.g['bufferViews']) - 1, componentType=comp, count=len(flat) // w, type=typ)
        if norm:
            a['normalized'] = True
        if minmax:
            a['min'] = [min(flat[i::w]) for i in range(w)]
            a['max'] = [max(flat[i::w]) for i in range(w)]
        self.g['accessors'].append(a)
        return len(self.g['accessors']) - 1

    def texture(self, tex, gidx, near):
        hit = tex.find(gidx, near)
        if not hit:
            self.notes.append('texture %08x not found' % gidx)
            return None
        rgba, w, h, fmt, name, src = hit
        if name in self.tex_by_name:
            return self.tex_by_name[name]
        fn = name + '.png'
        nud.write_png(os.path.join(self.dir, fn), rgba, w, h)
        self.g['images'].append(dict(uri=fn, name='%08x' % gidx))
        self.g['textures'].append(dict(source=len(self.g['images']) - 1, sampler=0, name='%08x %s' % (gidx, src)))
        i = len(self.g['textures']) - 1
        self.tex_by_name[name] = (i, fmt, has_alpha(rgba))
        return self.tex_by_name[name]

    def material(self, part, tex, near, lod):
        texs = part.textures()
        key = (tuple(texs), part.blend_src, part.blend_dst, part.blend_eq, part.alpha_func, part.alpha_ref,
               part.flags & 4, tuple(part.diffuse))
        if key in self.mat_by_key:
            return self.mat_by_key[key]
        pbr = dict(metallicFactor=0.0, roughnessFactor=0.9)
        m = dict(name='', pbrMetallicRoughness=pbr, doubleSided=not part.flags & 4)
        d = part.diffuse
        alpha = d[3] if part.blend_eq and d[3] > 0 else 1.0
        if tuple(d[:3]) != (1.0, 1.0, 1.0) or alpha != 1.0:
            pbr['baseColorFactor'] = [d[0], d[1], d[2], alpha]
        t = None
        if texs and texs[0][0] < 0xF0000000:
            t = self.texture(tex, texs[0][0], near)
            if t:
                pbr['baseColorTexture'] = dict(index=t[0], texCoord=(texs[0][1] >> 10) & 3)
        extra = []
        for g, prm in texs[1:]:                   # layers 1-3 (multi-layer parts): PNG written, listed in extras
            if g < 0xF0000000:
                tt = self.texture(tex, g, near)
                if tt:
                    extra.append(dict(texture=tt[0], gidx='%08x' % g, op=(prm >> 12) & 0xF, texCoord=(prm >> 10) & 3))
        m['name'] = ('%08x' % texs[0][0]) if texs else 'untextured'
        if part.blend_eq:
            m['alphaMode'] = 'BLEND'
            m['name'] += '_blend' if part.blend_dst != 1 else '_add'
        elif part.alpha_func >= 0x200 and part.alpha_func != 0x207 and t and t[2]:
            m['alphaMode'] = 'MASK'
            m['alphaCutoff'] = max(part.alpha_ref / 255.0, 0.02)
        st = part.state()
        st.update(texture_layers=['%08x/%08x' % x for x in texs], shade=part.shade, flags='%x/%x' % (part.flags_a,
                  part.flags), ambient=part.ambient, specular=part.specular, emissive=part.emissive, power=part.power)
        if extra:
            st['extra_layers'] = extra
        m['extras'] = st
        self.g['materials'].append(m)
        self.mat_by_key[key] = len(self.g['materials']) - 1
        return self.mat_by_key[key]

    def write(self):
        self.blob += b'\0' * (-len(self.blob) % 4)
        binname = self.stem + '.bin'
        with open(os.path.join(self.dir, binname), 'wb') as f:
            f.write(self.blob)
        self.g['buffers'] = [dict(uri=binname, byteLength=len(self.blob))]
        for k in ('textures', 'images', 'materials', 'meshes'):
            if not self.g[k]:
                del self.g[k]
        if 'textures' not in self.g:
            del self.g['samplers']
        with open(self.path, 'w') as f:
            json.dump(self.g, f, indent=1)


def has_alpha(rgba):
    return any(a < 250 for a in rgba[3::4])


# ---- skeleton --------------------------------------------------------------------------------------------------
class Skeleton:
    """24-track body rig (or any rig with matching INITIAL_TRANS / INITIAL_ROT), game space."""

    def __init__(self, romdata, trans_id=1101, rot_id=1100):
        self.trans, self.rot = A.load(romdata, trans_id), A.load(romdata, rot_id)
        if self.trans.ntracks != self.rot.ntracks:
            raise ValueError('INITIAL_TRANS %d and INITIAL_ROT %d differ in track count' % (trans_id, rot_id))
        self.n = n = self.rot.ntracks
        self.parents = self.rot.parents
        self.joint_to_track = {}
        for tr in self.rot.tracks:
            if tr['type'] == 7:
                self.joint_to_track.setdefault(tr['joint'], tr['index'])
        self.root_rest = self.trans.pose(0)[0]['t']
        self.rest_t, self.rest_r = [], []
        for t in range(n):
            tt = list(self.trans.pose(0)[t]['t'])
            if t == 1 or (self.parents[t] == 0 and self.rot.tracks[t]['type'] == 7):
                tt = [a - b for a, b in zip(tt, self.root_rest)]   # the game stacks the root offset twice
            self.rest_t.append(tt)
            self.rest_r.append(self.rot.pose(0)[t]['r'] or (0, 0, 0))
        self.world = A.world(self.rot, [(self.rest_t[t], self.rest_r[t], None) for t in range(n)])
        self.names = A.BODY24 if n == 24 else {}

    def track_of(self, joint):
        if self.n == 24 and joint in (22, 23):
            # body models skin the wrist ends of the sleeves/arms to joints 22 / 23. Their vertices sit at the
            # left / right wrist and blend from the forearm (joint 7 / 11) to 100 % at the hand, in every body
            # (Alex, T'Lan, soldiers). In the animations joint 22 is the unused 'extra' track, so these are the
            # hands (where the separate hand models hang). Mapping seen in the data, not traced in the code.
            return 9 if joint == 22 else 13
        return self.joint_to_track.get(joint, 1)


def add_skeleton(gl, sk, anim=None, fps=60.0):
    """nodes 0.. for the tracks, a skin; returns (skin index, root node list)."""
    base = len(gl.g['nodes'])
    for t in range(sk.n):
        nd = dict(name='%02d_%s' % (t, sk.names.get(t, 'track')), translation=list(cpos(sk.rest_t[t])),
                  rotation=list(cquat(quat_euler(sk.rest_r[t]))))
        kids = [base + c for c in range(sk.n) if sk.parents[c] == t]
        if kids:
            nd['children'] = kids
        gl.g['nodes'].append(nd)
    ibm = []
    for t in range(sk.n):
        mi = inv_rigid(cmat(sk.world[t]))
        ibm.append([mi[r][c] for c in range(4) for r in range(4)])
    a_ibm = gl.add(ibm, 'f', 5126, 'MAT4')
    gl.g.setdefault('skins', []).append(dict(joints=list(range(base, base + sk.n)), inverseBindMatrices=a_ibm,
                                             skeleton=base))
    roots = [base + t for t in range(sk.n) if sk.parents[t] == -1]
    if anim is not None:
        if anim.ntracks != sk.n:
            raise ValueError('animation has %d tracks, rig has %d' % (anim.ntracks, sk.n))
        times, tkeys, rkeys, prev = [], [], [[] for _ in range(sk.n)], [None] * sk.n
        for k in range(anim.nkeys):
            times.append(anim.key_frame(k) / fps)
            pose = anim.pose(k)
            tkeys.append(cpos(pose[0]['t'] or sk.root_rest))
            for t in range(sk.n):
                r = pose[t]['r'] if anim.tracks[t]['type'] in (4, 5, 6, 7) and pose[t]['r'] else sk.rest_r[t]
                q = cquat(quat_euler(r))
                if prev[t] and sum(a * b for a, b in zip(q, prev[t])) < 0:
                    q = tuple(-x for x in q)
                prev[t] = q
                rkeys[t].append(q)
        a_time = gl.add(times, 'f', 5126, 'SCALAR', minmax=True)
        samplers = [dict(input=a_time, output=gl.add(tkeys, 'f', 5126, 'VEC3'), interpolation='LINEAR')]
        channels = [dict(sampler=0, target=dict(node=base, path='translation'))]
        for t in range(sk.n):
            samplers.append(dict(input=a_time, output=gl.add(rkeys[t], 'f', 5126, 'VEC4'), interpolation='LINEAR'))
            channels.append(dict(sampler=len(samplers) - 1, target=dict(node=base + t, path='rotation')))
        gl.g.setdefault('animations', []).append(dict(name=anim.name, samplers=samplers, channels=channels))
    return len(gl.g['skins']) - 1, roots


# ---- one model -> one mesh ---------------------------------------------------------------------------------------
# models skinned to their own small rig (hair, face) are fixed to one body bone instead; hand models get
# their joints sorted onto upper arm / forearm / hand by position (hand_joint_map)
ATTACH = (('hair', 5), ('_head', 5), ('matsuge', 5), ('_face', 5))
HAND = (('_hand_l', (7, 8, 9)), ('_hand_r', (11, 12, 13)))


def hand_joint_map(nudm, sk, arm):
    """Hand models use their own joint list: some arm joints, the palm, 15 finger joints (the hand rig,
    INITIAL_HAND_*), and a forearm joint; the numbering shifts between characters (Derrick: palm 2, Alex: palm 1).
    Each joint goes to the body bone its vertices sit on, by distance along the arm (arms lie along X in the rest
    pose): past the wrist (minus 0.3) -> hand, past the elbow -> forearm, else upper arm. Fingers follow the hand."""
    up, fo, ha = arm
    ex, hx = abs(sk.world[fo][0][3]), abs(sk.world[ha][0][3])
    sums = {}
    for p in nudm.parts:
        if not (p.fmt and p.fmt & nud.F_SKIN and p.bones):
            continue
        v = p.vertices()
        for pos, jj, ww in zip(v['pos'], v['joints'], v['weights']):
            for j, w in zip(jj, ww):
                if w > 0.5 and j < len(p.bones):
                    s = sums.setdefault(p.bones[j], [0.0, 0])
                    s[0] += abs(pos[0])
                    s[1] += 1
    out = {}
    for j, (sx, n) in sums.items():
        x = sx / n
        out[j] = ha if x >= hx - 0.3 else fo if x >= ex else up
    return out


def attach_track(name, override=None):
    if override is not None:
        return override
    for key, t in ATTACH:
        if key in name.lower():
            return t
    return None


def hand_arm(name):
    for key, arm in HAND:
        if key in name.lower():
            return arm
    return None


def add_model(gl, tex, entry, data, lod=0, sk=None, skin=None, attach=None):
    model = nud.Model(entry, data)
    n = model.lod(lod) if model.lod_offsets[lod] >= 0 else None
    if n is None:
        gl.notes.append('%s: no model in LOD slot %d' % (entry['name'], lod))
        return None
    prims, info = [], dict(parts=0, verts=0, tris=0, skinned=False)
    arm = hand_arm(entry['name']) if sk is not None and attach is None else None
    hmap = hand_joint_map(n, sk, arm) if arm else None
    if hmap:
        gl.notes.append('%s: hand rig joints mapped by position to %s' % (
            entry['name'], ', '.join('%d->%s' % (j, sk.names.get(t, t)) for j, t in sorted(hmap.items()))))
    for p in n.parts:
        if not p.tris:
            continue
        v = p.vertices()
        attrs = dict(POSITION=gl.add([cpos(x) for x in v['pos']], 'f', 5126, 'VEC3', 34962, minmax=True))
        if 'normal' in v:
            attrs['NORMAL'] = gl.add([cdir(x) for x in v['normal']], 'f', 5126, 'VEC3', 34962)
        if 'color' in v:
            attrs['COLOR_0'] = gl.add(v['color'], 'B', 5121, 'VEC4', 34962, norm=True)
        for k, name in (('uv0', 'TEXCOORD_0'), ('uv1', 'TEXCOORD_1')):
            if k in v:
                attrs[name] = gl.add(v[k], 'f', 5126, 'VEC2', 34962)
        if 'weights' in v and sk is not None and p.bones:
            js, ws = [], []
            for jj, ww in zip(v['joints'], v['weights']):
                if attach is not None:
                    js.append([attach, 0, 0, 0])
                    ws.append([1.0, 0.0, 0.0, 0.0])
                    continue
                if hmap:
                    js.append([hmap.get(p.bones[k] if k < len(p.bones) else -1, arm[2]) for k in jj])
                else:
                    js.append([sk.track_of(p.bones[k] if k < len(p.bones) else 0) for k in jj])
                s = sum(ww) or 1.0
                ws.append([x / s for x in ww])
            attrs['JOINTS_0'] = gl.add(js, 'H', 5123, 'VEC4', 34962)
            attrs['WEIGHTS_0'] = gl.add(ws, 'f', 5126, 'VEC4', 34962)
            info['skinned'] = True
        ind = [i for a, b, c in p.tris if max(a, b, c) < p.nverts for i in (a, c, b)]   # mirror flips winding
        prims.append(dict(attributes=attrs, indices=gl.add(ind, 'I', 5125, 'SCALAR', 34963),
                          material=gl.material(p, tex, entry['pak'], lod),
                          extras=dict(part=p.index, vertex_format='%02x' % p.fmt)))
        info['parts'] += 1
        info['verts'] += p.nverts
        info['tris'] += len(ind) // 3
    if not prims:
        return None
    gl.g['meshes'].append(dict(name=entry['name'], primitives=prims))
    node = dict(name=entry['name'], mesh=len(gl.g['meshes']) - 1,
                extras=dict(model_id=entry['id'], pak=os.path.basename(entry['pak']), lod=lod))
    if attach is not None and info['skinned']:
        node['extras']['fixed_to_bone'] = '%02d_%s' % (attach, sk.names.get(attach, 'track'))
        gl.notes.append('%s: own rig not exported, fixed to bone %s' % (entry['name'], node['extras']['fixed_to_bone']))
    if info['skinned']:
        node['skin'] = skin
    gl.g['nodes'].append(node)
    gl.g['scenes'][0]['nodes'].append(len(gl.g['nodes']) - 1)
    return info


# collision colours by triangle type (low 4 bits of the surface word), RGBA; alpha makes it see-through
HIT_COLOURS = {0: (1.0, 0.45, 0.05, 0.35), 1: (0.6, 0.2, 0.9, 0.45), 2: (0.1, 0.45, 1.0, 0.45),
               3: (0.1, 0.85, 0.3, 0.45), 4: (0.6, 0.2, 0.9, 0.45), 15: (0.5, 0.5, 0.5, 0.3)}


def hit_material(gl, typ):
    key = ('hit', typ)
    if key not in gl.mat_by_key:
        col = HIT_COLOURS.get(typ, (1.0, 0.0, 1.0, 0.45))
        gl.g['materials'].append(dict(
            name='hit_type%d' % typ, alphaMode='BLEND', doubleSided=True,
            pbrMetallicRoughness=dict(baseColorFactor=list(col), metallicFactor=0.0, roughnessFactor=1.0),
            emissiveFactor=[c * 0.5 for c in col[:3]],
            extras=dict(collision_type=typ, meaning=nud.HIT_TYPES.get(typ, '?'))))
        gl.mat_by_key[key] = len(gl.g['materials']) - 1
    return gl.mat_by_key[key]


def add_collision(gl, entry, hits):
    """one mesh per model entry with all its HD51 blocks: a triangle primitive per triangle type, flat
    (unshared) vertices with face normals, and a LINES primitive for the AEG edges."""
    prims, info = [], dict(blocks=len(hits), tris=0, edges=0)
    by_type = {}
    for h in hits:
        for a, b, c, w in h.tris:
            by_type.setdefault(nud.surface(w)[0], []).append((h.points[a], h.points[b], h.points[c], w))
    for typ, tris in sorted(by_type.items()):
        pos, nrm = [], []
        for a, b, c, _w in tris:
            pa, pb, pc = cpos(a), cpos(b), cpos(c)
            u = [pb[i] - pa[i] for i in range(3)]
            v = [pc[i] - pa[i] for i in range(3)]
            n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            ln = math.sqrt(sum(x * x for x in n)) or 1.0
            n = tuple(x / ln for x in n)
            pos += [pa, pc, pb]                      # mirror flips winding
            nrm += [tuple(-x for x in n)] * 3
        mats = sorted(set(nud.surface(t[3])[1] for t in tris))
        prims.append(dict(attributes=dict(POSITION=gl.add(pos, 'f', 5126, 'VEC3', 34962, minmax=True),
                                          NORMAL=gl.add(nrm, 'f', 5126, 'VEC3', 34962)),
                          material=hit_material(gl, typ),
                          extras=dict(collision_type=typ, triangles=len(tris), materials=mats,
                                      surface_words=sorted(set('%04x' % t[3] for t in tris)))))
        info['tris'] += len(tris)
    lines = [cpos(h.points[i]) for h in hits for e in h.edges for i in e[:2]]
    if lines:
        if ('hit', 'edge') not in gl.mat_by_key:
            gl.g['materials'].append(dict(name='hit_edge', pbrMetallicRoughness=dict(
                baseColorFactor=[1, 0, 0, 1], metallicFactor=0.0), emissiveFactor=[1, 0, 0]))
            gl.mat_by_key[('hit', 'edge')] = len(gl.g['materials']) - 1
        prims.append(dict(attributes=dict(POSITION=gl.add(lines, 'f', 5126, 'VEC3', 34962, minmax=True)),
                          mode=1, material=gl.mat_by_key[('hit', 'edge')], extras=dict(aeg_edges=len(lines) // 2)))
        info['edges'] = len(lines) // 2
    if not prims:
        return None
    name = entry['name'] + ' [hit]'
    gl.g['meshes'].append(dict(name=name, primitives=prims))
    gl.g['nodes'].append(dict(name=name, mesh=len(gl.g['meshes']) - 1, extras=dict(
        model_id=entry['id'], pak=os.path.basename(entry['pak']), collision=True,
        hd51=['%#x' % h.offset for h in hits])))
    gl.g['scenes'][0]['nodes'].append(len(gl.g['nodes']) - 1)
    return info


def model_is_skinned(entry, data, lod):
    m = nud.Model(entry, data)
    n = m.lod(lod) if m.lod_offsets[lod] >= 0 else None
    return bool(n) and any(p.fmt and p.fmt & nud.F_SKIN for p in n.parts)


def find_section_pak(romdata, section):
    for r, _, fs in os.walk(os.path.join(romdata, 'area')):
        if section + '.pak' in fs:
            return os.path.join(r, section + '.pak')
    raise KeyError('no %s.pak under area/' % section)


def export(romdata, ids, out, pak=None, lod=0, anim_id=None, trans_id=1101, rot_id=1100, fps=60.0, area=None,
           attach=None, collision=False, collision_only=False):
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    gl, tex = Gltf(out), nud.TexIndex(romdata)
    models = []
    if area:
        p = find_section_pak(romdata, area)
        with open(p, 'rb') as f:
            d = f.read()
        models = [(e, d[e['off']:e['end']]) for e in nud.model_entries(p) if e['scenery'] and e['lods'][lod] >= 0]
    else:
        models = [nud.find_model(romdata, i, pak) for i in ids]
    infos = {}
    if collision or collision_only:
        if area:
            hits = nud.section_collision(romdata, area)
        else:
            hits = [(e, nud.hit_blocks(e, open(e['pak'], 'rb').read())) for e, _ in models]
        for e, hs in hits:
            infos['%d %s [hit]' % (e['id'], e['name'])] = add_collision(gl, e, hs)
        if collision_only:
            gl.write()
            return infos, gl
    sk = skin = None
    if any(model_is_skinned(e, d, lod) for e, d in models):
        sk = Skeleton(romdata, trans_id, rot_id)
        anim = A.load(romdata, anim_id) if anim_id else None
        skin, roots = add_skeleton(gl, sk, anim, fps)
        gl.g['scenes'][0]['nodes'] += roots
    for e, d in models:
        at = attach_track(e['name'], (attach or {}).get(e['id'])) if sk else None
        infos['%d %s' % (e['id'], e['name'])] = add_model(gl, tex, e, d, lod, sk, skin, at)
    gl.write()
    return infos, gl


def main(a):
    if len(a) < 3:
        print(__doc__)
        return 1
    opt = lambda k, d: type(d)(a[a.index(k) + 1]) if k in a else d  # noqa: E731
    romdata = a[0]
    hit = dict(collision='--collision' in a, collision_only='--collision-only' in a)
    if a[1] == '--area':
        infos, gl = export(romdata, [], a[3], lod=opt('--lod', 0), area=a[2], **hit)
    else:
        ids = [int(x) for x in a[1].split(',')]
        infos, gl = export(romdata, ids, a[2], pak=opt('--pak', '') or None, lod=opt('--lod', 0),
                           anim_id=opt('--anim', 0) or None, trans_id=opt('--trans', 1101), rot_id=opt('--rot', 1100),
                           fps=opt('--fps', 60.0),
                           attach=dict(tuple(int(x) for x in kv.split(':')) for kv in opt('--attach', '').split(',') if kv),
                           **hit)
    for k, v in infos.items():
        print('%-55s %s' % (k, v))
    print('%d textures written next to %s' % (len(gl.tex_by_name), gl.path))
    for n in sorted(set(gl.notes)):
        print('note:', n)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
