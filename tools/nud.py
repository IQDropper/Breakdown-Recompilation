"""nud.py - read Breakdown's NUDX models (pak kind 1) and find their textures. Layout: docs/formats/nud.md.

  python tools/nud.py <romdata> <model id> [--pak PATH]        print one model (LODs, parts, materials)
  python tools/nud.py <romdata> --area hon_under_A              list the scenery models of one area section
  python tools/nud.py <romdata> --check                         read every model in the game, report problems

Library use (tools/model_gltf.py): find_model(), Model(entry bytes).lod(n) -> parts with vertices and triangles,
TexIndex(romdata).find(gidx, near=pak) -> (rgba bytes, w, h) of the texture a material names.
"""
import bisect
import collections
import json
import math
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tex_extract as T  # noqa: E402

# ---- pak entries ------------------------------------------------------------------------------------------------
ENTRY = 0x80


def model_entries(path):
    """[dict] for every model entry (kind byte 1) of one pak. byte 3 = 1 marks area scenery ('obj_in_...')."""
    d = open(path, 'rb').read()
    if d[:4] != b'BDPK':
        return []
    (n,) = struct.unpack_from('<I', d, 4)
    starts = sorted(struct.unpack_from('<I', d, 0x40 + i * ENTRY + 0x10)[0] for i in range(n)) + [len(d)]
    out = []
    for i in range(n):
        o = 0x40 + i * ENTRY
        if d[o:o + 2] != b'BD' or d[o + 2] != 1:
            continue
        mid, size, _z, off = struct.unpack_from('<4I', d, o + 4)
        lods = list(struct.unpack_from('<8i', d, o + 0x58))
        hit_a, hit_b = struct.unpack_from('<2i', d, o + 0x78)
        out.append(dict(id=mid, scenery=d[o + 3] == 1, size=size, off=off,
                        end=starts[bisect.bisect_right(starts, off)],
                        name=d[o + 0x14:o + 0x40].split(b'\0')[0].decode('latin-1'),
                        nud_size=struct.unpack_from('<I', d, o + 0x50)[0],
                        hit_off=struct.unpack_from('<I', d, o + 0x4C)[0], hit_size=struct.unpack_from('<I', d, o + 0x54)[0],
                        lods=lods, hit_a=hit_a, hit_b=hit_b, pak=path))
    return out


def all_paks(romdata):
    paks = []
    for r, _, fs in os.walk(romdata):
        paks += [os.path.join(r, f) for f in fs if f.endswith('.pak')]
    paks.sort(key=lambda p: (not p.replace('\\', '/').endswith('common/base.pak'), p))
    return paks


def find_model(romdata, model_id, pak=None):
    """(entry dict, entry bytes). Searches base.pak first, then every pak (or only `pak`)."""
    for p in [pak] if pak else all_paks(romdata):
        for e in model_entries(p):
            if e['id'] == model_id:
                with open(p, 'rb') as f:
                    f.seek(e['off'])
                    return e, f.read(e['end'] - e['off'])
    raise KeyError('model %d not found' % model_id)


# ---- NUDX ------------------------------------------------------------------------------------------------------
# vertex format = low byte of the 0x20xx chunk type. Bits: 1 normal, 2 colour, 4 uv0, 8 skin, 0x10 uv1.
F_NORMAL, F_COLOR, F_UV0, F_SKIN, F_UV1 = 1, 2, 4, 8, 0x10
PRIM = {5: 'triangles', 6: 'strip', 7: 'fan', 8: 'quads', 9: 'quad_strip', 10: 'polygon'}
BLEND = {0: 'ZERO', 1: 'ONE', 0x300: 'SRC_COLOR', 0x301: 'ONE_MINUS_SRC_COLOR', 0x302: 'SRC_ALPHA',
         0x303: 'ONE_MINUS_SRC_ALPHA', 0x304: 'DST_ALPHA', 0x305: 'ONE_MINUS_DST_ALPHA', 0x306: 'DST_COLOR',
         0x307: 'ONE_MINUS_DST_COLOR'}
EQUATION = {0: 'off', 0x8006: 'ADD', 0x800A: 'SUBTRACT', 0x800B: 'REVERSE_SUBTRACT'}
CMP = {0x200: 'NEVER', 0x201: 'LESS', 0x202: 'EQUAL', 0x203: 'LEQUAL', 0x204: 'GREATER', 0x205: 'NOTEQUAL',
       0x206: 'GEQUAL', 0x207: 'ALWAYS'}


def vertex_layout(fmt):
    """[(name, offset, size)] and stride for one vertex format."""
    lay, o = [('pos', 0, 12)], 12
    for bit, name, size in ((F_NORMAL, 'normal', 8), (F_COLOR, 'color', 4), (F_UV0, 'uv0', 8), (F_UV1, 'uv1', 8),
                            (F_SKIN, 'skin', 16)):
        if fmt & bit:
            lay.append((name, o, size))
            o += size
    return lay, o


class Part:
    """One 0xA0 part header + its geometry."""

    def __init__(self, b, base, index):
        q = base + 0x20 + 0xA0 * index
        self.index = index
        (self.off, self.size) = struct.unpack_from('<iI', b, q)
        (self.shade, self.flags_a, self.flags) = struct.unpack_from('<HHI', b, q + 8)
        self.sphere = struct.unpack_from('<4f', b, q + 0x10)
        c = struct.unpack_from('<16f', b, q + 0x20)
        self.diffuse, self.ambient, self.specular, self.emissive = c[0:4], c[4:8], c[8:12], c[12:16]
        (self.power,) = struct.unpack_from('<f', b, q + 0x60)
        (self.blend_src, self.blend_dst, self.blend_eq, self.alpha_ref, self.alpha_func,
         self.depth_func) = struct.unpack_from('<6H', b, q + 0x64)
        self.layers = [struct.unpack_from('<II', b, q + 0x70 + 8 * k) for k in range(4)]
        self.bones = [x for x in b[q + 0x90:q + 0xA0] if x != 0xFF]
        self.shared = None
        if self.off < 0:                     # same geometry as an earlier part (loader 0x19A8C0 / 0x18E020)
            self.shared = (q + self.off - base - 0x20) // 0xA0
            return
        self._geometry(b, base + self.off)

    # textures the game binds: one layer unless flags_a bit 15 (then up to 4, 0x197570)
    def textures(self):
        n = 4 if self.flags_a & 0x8000 else 1
        return [(g, p) for g, p in self.layers[:n] if g != 0xFFFFFFFF]

    def state(self):
        s = dict(cull=bool(self.flags & 4), depth_write=not self.flags & 0x20000,
                 depth_test=CMP.get(self.depth_func, 'off'),
                 alpha_test=CMP.get(self.alpha_func, 'off') if self.alpha_func >= 0x200 else 'off',
                 alpha_ref=self.alpha_ref, blend=EQUATION.get(self.blend_eq, hex(self.blend_eq)))
        if self.blend_eq:
            s['blend'] += ' %s %s' % (BLEND.get(self.blend_src, hex(self.blend_src)),
                                      BLEND.get(self.blend_dst, hex(self.blend_dst)))
        return s

    def _geometry(self, b, c):
        self.fmt, self.verts, self.tris, self.prims = None, None, [], collections.Counter()
        while c + 16 <= len(b):
            typ, s16, _u, count = struct.unpack_from('<4I', b, c)
            if typ == 0xFFFF or s16 == 0:
                break
            if typ & 0xFF00 == 0x2000:                       # vertex buffer: 2 header blocks, then vertices
                self.fmt, self.nverts = typ & 0xFF, count
                self.layout, self.stride = vertex_layout(self.fmt)
                self.vdata = (c + 0x20, b)
            elif typ & 0xFF00 == 0x3000:                     # push buffer: D3DPushBuffer at +0x10, data at +0x30
                (pb_size,) = struct.unpack_from('<I', b, c + 0x1C)
                self._pushbuf(b, c + 0x30, c + 0x30 + pb_size)
            c += s16 * 16

    def _pushbuf(self, b, p, end):
        """NV2A methods: 0x17FC begin/end (primitive), 0x1800 16-bit index pairs, 0x1808 32-bit indices,
        0x1810 runs (start, count-1 in the top byte)."""
        prim, idx = None, []
        while p + 4 <= end:
            (h,) = struct.unpack_from('<I', b, p)
            p += 4
            meth, cnt = h & 0x1FFC, (h >> 18) & 0x7FF
            if meth == 0x17FC:
                (v,) = struct.unpack_from('<I', b, p)
                if v == 0 and prim is not None:
                    self._emit(prim, idx)
                    prim, idx = None, []
                else:
                    prim, idx = v, []
            elif meth == 0x1800:
                for k in range(cnt):
                    (v,) = struct.unpack_from('<I', b, p + 4 * k)
                    idx += [v & 0xFFFF, v >> 16]
            elif meth == 0x1808:
                idx += struct.unpack_from('<%dI' % cnt, b, p)
            elif meth == 0x1810:
                for k in range(cnt):
                    (v,) = struct.unpack_from('<I', b, p + 4 * k)
                    idx += list(range(v & 0xFFFFFF, (v & 0xFFFFFF) + (v >> 24) + 1))
            p += 4 * cnt

    def _emit(self, prim, idx):
        self.prims[PRIM.get(prim, prim)] += 1
        t = self.tris
        if prim == 5:
            t += [tuple(idx[i:i + 3]) for i in range(0, len(idx) - 2, 3)]
        elif prim == 6:
            for i in range(len(idx) - 2):
                a, c, d = idx[i:i + 3]
                if a != c and c != d and a != d:
                    t.append((a, c, d) if i % 2 == 0 else (c, a, d))
        elif prim in (7, 10):
            t += [(idx[0], idx[i], idx[i + 1]) for i in range(1, len(idx) - 1)]
        elif prim == 8:
            for i in range(0, len(idx) - 3, 4):
                a, c, d, e = idx[i:i + 4]
                t += [(a, c, d), (a, d, e)]
        elif prim == 9:
            for i in range(0, len(idx) - 3, 2):
                a, c, d, e = idx[i:i + 4]
                t += [(a, c, e), (a, e, d)]

    def vertices(self):
        """dict of lists: pos, normal, color (RGBA 0..255), uv0, uv1, weights (4 floats), joints (4 bone-list slots)."""
        o0, b = self.vdata
        out = collections.defaultdict(list)
        for i in range(self.nverts):
            o = o0 + i * self.stride
            for name, off, _size in self.layout:
                a = o + off
                if name == 'pos':
                    out['pos'].append(struct.unpack_from('<3f', b, a))
                elif name == 'normal':
                    n = struct.unpack_from('<3h', b, a)
                    out['normal'].append(tuple(x / 32767.0 for x in n))
                elif name == 'color':
                    bb, g, r, al = b[a:a + 4]
                    out['color'].append((r, g, bb, al))
                elif name in ('uv0', 'uv1'):
                    out[name].append(struct.unpack_from('<2f', b, a))
                elif name == 'skin':
                    w = struct.unpack_from('<4h', b, a)
                    out['weights'].append(tuple(max(0, x) / 32767.0 for x in w))
                    out['joints'].append(struct.unpack_from('<4H', b, a + 8))
        return out


class Nud:
    def __init__(self, b, base=0):
        if b[base:base + 4] != b'NUDX':
            raise ValueError('no NUDX at %#x' % base)
        self.size, = struct.unpack_from('<I', b, base + 4)
        self.version, self.nparts = struct.unpack_from('<HH', b, base + 8)
        self.sphere = struct.unpack_from('<4f', b, base + 0x10)
        self.parts = [Part(b, base, i) for i in range(self.nparts)]
        for p in self.parts:                  # shared parts take the geometry of the part they point at
            if p.shared is not None:
                src = self.parts[p.shared]
                for k in ('fmt', 'nverts', 'layout', 'stride', 'vdata', 'tris', 'prims'):
                    setattr(p, k, getattr(src, k))


class Model:
    """One model entry: up to 8 LOD slots (entry +0x58), each a NUDX inside the entry data."""

    def __init__(self, entry, data):
        self.entry, self.data = entry, data
        self.lod_offsets = entry['lods']

    def lod(self, n=0):
        off = self.lod_offsets[n]
        if off < 0:
            return None
        return Nud(self.data, off)

    def distinct_lods(self):
        seen, out = {}, []
        for i, o in enumerate(self.lod_offsets):
            if o >= 0 and o not in seen:
                seen[o] = i
                out.append(i)
        return out


# ---- textures --------------------------------------------------------------------------------------------------
class TexIndex:
    """Every TIM2 picture in the game that carries a GIDX tag (in the picture header extension, id at tag+8).
    A model layer's texture value is that id (0x10000000 | n). Cached in %TEMP%."""

    def __init__(self, romdata, cache=None):
        self.romdata = os.path.abspath(romdata)
        cache = cache or os.path.join(os.environ.get('TEMP', '/tmp'), 'breakdown_gidx_index.json')
        files = sorted(os.path.join(r, f) for r, _, fs in os.walk(self.romdata) for f in fs
                       if f.endswith(('.pak', '.nut')))
        stamp = [[os.path.relpath(f, self.romdata), os.path.getsize(f)] for f in files]
        try:
            with open(cache) as fh:
                c = json.load(fh)
            if c['stamp'] == stamp:
                self.idx = {int(k): v for k, v in c['idx'].items()}
                return
        except (OSError, ValueError, KeyError):
            pass
        self.idx = collections.defaultdict(list)
        for f in files:
            d = open(f, 'rb').read()
            rel = os.path.relpath(f, self.romdata).replace('\\', '/')
            for m in re.finditer(b'TIM2', d):
                base = m.start()
                (n,) = struct.unpack_from('<H', d, base + 6)
                o = base + (0x80 if d[base + 5] == 1 else 0x10)
                for _ in range(n):
                    if o + 0x30 > len(d):
                        break
                    tot, _cl, _im, hdr = struct.unpack_from('<IIIH', d, o)
                    if tot == 0 or o + tot > len(d) or hdr < 0x30:
                        break
                    k = d.find(b'GIDX', o + 0x30, o + hdr)   # +0x40, or +0x60 after a mipmap size table
                    if k > 0:
                        (g,) = struct.unpack_from('<I', d, k + 8)
                        self.idx[g].append([rel, o])
                    o += tot
        try:
            with open(cache, 'w') as fh:
                json.dump(dict(stamp=stamp, idx=self.idx), fh)
        except OSError:
            pass

    def candidates(self, gidx, near=None):
        """picture locations for an id, the ones closest to `near` (the model's pak) first: same file, same
        section's .nut (hon_under_A.pak -> hon_under_A.nut), same area, common/, anything."""
        c = self.idx.get(gidx, [])
        if not near:
            return c
        rel = os.path.relpath(os.path.abspath(near), self.romdata).replace('\\', '/')
        stem, area = rel.rsplit('.', 1)[0], os.path.dirname(rel)

        def rank(x):
            f = x[0]
            return (f != rel, f.rsplit('.', 1)[0] != stem, os.path.dirname(f) != area,
                    not f.startswith('common/'), f)
        return sorted(c, key=rank)

    def picture(self, rel, o):
        """(rgba bytes, w, h, xbox format, hash name) of the picture at file offset o."""
        with open(os.path.join(self.romdata, rel), 'rb') as f:
            f.seek(o)
            h = f.read(0x30)
            tot, clut, img, hdr, _cc, _pf, _mip, _ct, it, w, hh = struct.unpack_from('<IIIHHBBBBHH', h)
            f.seek(o)
            d = f.read(tot)
        fmt = T.TYPES.get(it)
        if fmt is None:
            return None
        im, cl = d[hdr:hdr + img], d[hdr + img:hdr + img + clut]
        hsh = T.tex_hash(im)
        if fmt == 0x0B and cl:
            hsh ^= (T.tex_hash(cl) * 31) & T.M64
        lv = T.level0(im, w, hh, fmt)
        bgra = T.decode_p8(lv, cl, w, hh) if fmt == 0x0B else T.decode_dxt(lv, w, hh, fmt == 0x0F)
        rgba = bytearray(bgra)
        rgba[0::4], rgba[2::4] = bgra[2::4], bgra[0::4]
        return bytes(rgba), w, hh, fmt, '%016x_%dx%d_%02X' % (hsh, w, hh, fmt)

    def find(self, gidx, near=None):
        for rel, o in self.candidates(gidx, near):
            p = self.picture(rel, o)
            if p:
                return p + (rel,)
        return None


def write_png(path, rgba, w, h):
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))

    def chunk(t, data):
        return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# ---- command line ----------------------------------------------------------------------------------------------
def describe(e, data):
    m = Model(e, data)
    print('model %d %s  (%s, %d bytes, pak %s)' % (e['id'], e['name'], 'scenery' if e['scenery'] else 'object',
                                                  len(data), os.path.basename(e['pak'])))
    print('  LOD slots:', ' '.join('%x' % x if x >= 0 else '-' for x in e['lods']),
          ' hit data:', 'yes' if e['hit_a'] >= 0 else 'no')
    for li in m.distinct_lods():
        n = m.lod(li)
        print('  LOD %d: %d parts, sphere %s' % (li, n.nparts, ['%.2f' % x for x in n.sphere]))
        for p in n.parts:
            tex = ', '.join('%08x/%08x' % t for t in p.textures())
            print('    part %d: fmt %02x %4d verts %4d tris %s shade %d flags %x/%x tex %s bones %s %s' % (
                p.index, p.fmt or 0, p.nverts, len(p.tris), dict(p.prims), p.shade, p.flags_a, p.flags, tex,
                p.bones, p.state()))


def check_all(romdata):
    tex = TexIndex(romdata)
    stats = collections.Counter()
    bad = []
    seen = set()
    for pak in all_paks(romdata):
        d = None
        for e in model_entries(pak):
            key = (e['scenery'], e['id'])
            if key in seen:
                continue
            seen.add(key)
            if d is None:
                d = open(pak, 'rb').read()
            data = d[e['off']:e['end']]
            m = Model(e, data)
            if not m.distinct_lods():
                stats['no model (hit data only)'] += 1
                continue
            stats['models'] += 1
            for li in m.distinct_lods():
                try:
                    n = m.lod(li)
                except ValueError as ex:
                    bad.append((pak, e['id'], li, str(ex)))
                    continue
                for p in n.parts:
                    stats['parts'] += 1
                    if not p.tris:
                        stats['parts without triangles'] += 1
                    if p.tris and max(max(t) for t in p.tris) >= p.nverts:
                        bad.append((pak, e['id'], li, 'index out of range in part %d' % p.index))
                    for g, _ in p.textures():
                        stats['texture refs'] += 1
                        if not tex.candidates(g):
                            stats['texture refs not found (LOD slot %s)' % ('7' if li == 7 else '0-6')] += 1
    for k, v in sorted(stats.items(), key=lambda kv: (-kv[1], kv[0]))[:25]:
        print('%8d  %s' % (v, k))
    for b in bad[:20]:
        print('PROBLEM', b)
    print('%d problems' % len(bad))
    return 0 if not bad else 1


def main(a):
    if not a:
        print(__doc__)
        return 1
    romdata = a[0]
    if '--check' in a:
        return check_all(romdata)
    if '--area' in a:
        sec = a[a.index('--area') + 1]
        for r, _, fs in os.walk(os.path.join(romdata, 'area')):
            if sec + '.pak' in fs:
                for e in model_entries(os.path.join(r, sec + '.pak')):
                    if e['scenery']:
                        print('%5d %-50s %s' % (e['id'], e['name'], 'model' if e['lods'][0] >= 0 else 'hit data only'))
        return 0
    pak = a[a.index('--pak') + 1] if '--pak' in a else None
    e, data = find_model(romdata, int(a[1]), pak)
    describe(e, data)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
