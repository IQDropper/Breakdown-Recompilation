"""make_tex_pack.py - build a texture pack (XRTP v2) from replacement images named by texture hash.

1. Dump the game's textures: run with RECOMP_TEX_DUMP=<folder>. Each texture is saved once as
   <hash16>_<w>x<h>_<fmt>.tga, where hash16 is the key the pack uses.
2. Edit or upscale the ones you want. Keep the name's first 16 hex digits (the hash); the size can change
   (an upscaled 4x image is fine). Formats read: .tga (32/24-bit, uncompressed), .png (8-bit RGBA/RGB/grey,
   non-interlaced), .dds (A8R8G8B8 or DXT5/BC3, with its mipmaps).
3. python tools/make_tex_pack.py <images folder> <out.pack> [--include other.pack ...] [--no-mips] [--kind art|font|text]
   Then run the game with RECOMP_TEXTURES_PACK=<out.pack> (absolute, or relative to the exe's folder).

Alpha is kept. Mipmaps are made with a 2x2 box filter (DDS keeps its own). --include copies the entries of
another pack (e.g. build-win/menu_labels.pack from make_menu_labels.py), because the game loads one pack.
Standard library only. The pack and the images come from your own game files: never commit them.
Format: external/xboxrecomp/docs/formats/texture-pack.md."""
import os
import re
import struct
import sys
import zlib

KINDS = {"art": 0, "font": 1, "text": 2}
FLAG_BC3 = 0x10
HASH_RE = re.compile(r"^([0-9A-Fa-f]{16})")


def read_tga(path):
    d = open(path, "rb").read()
    idlen, cmtype, itype = d[0], d[1], d[2]
    w, h, bpp, desc = struct.unpack_from("<HHBB", d, 12)
    if cmtype != 0 or itype != 2 or bpp not in (24, 32):
        raise ValueError("only uncompressed 24/32-bit TGA")
    o = 18 + idlen
    n = bpp // 8
    rows = [d[o + y * w * n:o + (y + 1) * w * n] for y in range(h)]
    if not desc & 0x20:   # bottom-up
        rows.reverse()
    out = bytearray(w * h * 4)
    for y, r in enumerate(rows):
        for x in range(w):
            b, g, rr = r[x * n], r[x * n + 1], r[x * n + 2]
            a = r[x * n + 3] if n == 4 else 255
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((rr, g, b, a))
    return w, h, bytes(out)


def read_png(path):
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    o, idat, w = 8, b"", 0
    while o < len(d):
        ln, typ = struct.unpack_from(">I4s", d, o)
        body = d[o + 8:o + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if depth != 8 or inter != 0 or ctype not in (0, 2, 4, 6):
                raise ValueError("PNG must be 8-bit, non-interlaced, grey/RGB/RGBA")
        elif typ == b"IDAT":
            idat += body
        o += 12 + ln
    ch = {0: 1, 2: 3, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * ch
    prev = bytearray(stride)
    out = bytearray(w * h * 4)
    p = 0
    for y in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(w):
            px = line[x * ch:x * ch + ch]
            if ch == 1:
                rgba = (px[0], px[0], px[0], 255)
            elif ch == 2:
                rgba = (px[0], px[0], px[0], px[1])
            elif ch == 3:
                rgba = (px[0], px[1], px[2], 255)
            else:
                rgba = tuple(px)
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(rgba)
        prev = line
    return w, h, bytes(out)


def read_dds(path):
    """Returns (w, h, levels, payload, bc3). payload is BGRA texels (A8R8G8B8) or BC3 blocks, all levels."""
    d = open(path, "rb").read()
    if d[:4] != b"DDS ":
        raise ValueError("not a DDS")
    h, w = struct.unpack_from("<II", d, 12)
    mips = max(1, struct.unpack_from("<I", d, 28)[0])
    pf_flags, fourcc, bits = struct.unpack_from("<I4sI", d, 80)
    body = d[128:]
    if pf_flags & 0x4 and fourcc == b"DXT5":
        return w, h, mips, body, True
    if pf_flags & 0x40 and bits == 32:
        return w, h, mips, body, False
    raise ValueError("DDS must be DXT5 or 32-bit A8R8G8B8")


def to_bgra(rgba):
    b = bytearray(rgba)
    b[0::4], b[2::4] = rgba[2::4], rgba[0::4]
    return bytes(b)


def mip_chain(w, h, rgba):
    """Level 0 plus 2x2 box-filtered levels down to 1x1, as BGRA. Returns (levels, payload)."""
    levels, cur, cw, chh = [to_bgra(rgba)], rgba, w, h
    while cw > 1 or chh > 1:
        nw, nh = max(1, cw // 2), max(1, chh // 2)
        nxt = bytearray(nw * nh * 4)
        for y in range(nh):
            for x in range(nw):
                acc = [0, 0, 0, 0]
                n = 0
                for dy in (0, 1):
                    for dx in (0, 1):
                        sx, sy = min(cw - 1, 2 * x + dx), min(chh - 1, 2 * y + dy)
                        o = (sy * cw + sx) * 4
                        for k in range(4):
                            acc[k] += cur[o + k]
                        n += 1
                nxt[(y * nw + x) * 4:(y * nw + x) * 4 + 4] = bytes(v // n for v in acc)
        cur, cw, chh = bytes(nxt), nw, nh
        levels.append(to_bgra(cur))
    return len(levels), b"".join(levels)


def read_pack(path):
    """Entries of an existing XRTP pack: (hash, w, h, levels, flags, payload)."""
    d = open(path, "rb").read()
    if d[:4] != b"XRTP":
        raise ValueError("%s is not a texture pack" % path)
    ver, count, _scale, index = struct.unpack_from("<IIIQ", d, 4)
    offs = []
    for i in range(count):
        hsh, w, h, lv, fl, off = struct.unpack_from("<QIIIIQ", d, index + 32 * i)
        offs.append((hsh, w, h, lv, fl, off))
    out = []
    for i, (hsh, w, h, lv, fl, off) in enumerate(offs):
        end = offs[i + 1][5] if i + 1 < len(offs) else len(d)
        ends = sorted(o[5] for o in offs if o[5] > off)
        end = ends[0] if ends else len(d)
        out.append((hsh, w, h, lv, fl, d[off:end]))
    return out


def write_pack(path, entries):
    entries = sorted(entries, key=lambda e: e[0])
    hdr = 32
    off = hdr + 32 * len(entries)
    index, blobs = b"", []
    pos = off
    for hsh, w, h, lv, fl, payload in entries:
        index += struct.pack("<QIIIIQ", hsh, w, h, lv, fl, pos)
        blobs.append(payload)
        pos += len(payload)
    head = b"XRTP" + struct.pack("<IIIQII", 2, len(entries), 1, hdr, 1, 0)
    with open(path, "wb") as f:
        f.write(head + index + b"".join(blobs))


def main(argv):
    args, includes, mips, kind = [], [], True, 0
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--include":
            includes.append(argv[i + 1])
            i += 2
            continue
        if a == "--no-mips":
            mips = False
        elif a == "--kind":
            kind = KINDS[argv[i + 1]]
            i += 1
        else:
            args.append(a)
        i += 1
    if len(args) != 2:
        print(__doc__)
        return 2
    src, out = args
    entries = {}
    for inc in includes:
        for e in read_pack(inc):
            entries[e[0]] = e
        print("included %s" % inc)
    for name in sorted(os.listdir(src)):
        m = HASH_RE.match(name)
        ext = os.path.splitext(name)[1].lower()
        if not m or ext not in (".tga", ".png", ".dds"):
            continue
        hsh = int(m.group(1), 16)
        path = os.path.join(src, name)
        if ext == ".dds":
            w, h, lv, payload, bc3 = read_dds(path)
            fl = kind | (FLAG_BC3 if bc3 else 0)
        else:
            w, h, rgba = read_tga(path) if ext == ".tga" else read_png(path)
            if mips:
                lv, payload = mip_chain(w, h, rgba)
            else:
                lv, payload = 1, to_bgra(rgba)
            fl = kind
        entries[hsh] = (hsh, w, h, lv, fl, payload)
        print("%016X  %4dx%-4d  %2d level(s)  %s" % (hsh, w, h, lv, name))
    if not entries:
        print("[error] no images named <hash16>... in %s" % src)
        return 1
    write_pack(out, list(entries.values()))
    print("wrote %s: %d entries. Run with RECOMP_TEXTURES_PACK=%s" % (out, len(entries), os.path.abspath(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
