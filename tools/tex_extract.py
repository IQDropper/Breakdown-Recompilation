"""tex_extract.py - extract every texture from Breakdown's game files, named like RECOMP_TEX_DUMP, without playing.

The .pak / .nut files hold 'TIM2' containers whose pictures are Xbox texture data as the game uploads it (found
2026-11-13: hashing the stored bytes gives the same id the D3D11 backend computes in game, for 351 of the textures
dumped in the hospital rooms). So each picture's file name is the same as a dumped one and the existing tools
(upscale_tex.py, make_tex_pack.py, tex_review_sheets.py) work on the output unchanged.

TIM2 picture header (0x30 bytes): u32 total, clut bytes, image bytes, u16 header bytes, u16 clut colours,
u8 pic format, u8 mip levels, u8 clut type, u8 image type, u16 width, u16 height.
Image type -> Xbox format: 5 -> 0x0B P8 (swizzled indices, palette = the clut bytes, BGRA),
8 -> 0x0C DXT1, 12 -> 0x0F DXT5 (mip levels follow level 0). Other types are listed and skipped.
Hash (toolkit nv2a_tex_hash, seed 0xCBF29CE484222325) over the image bytes; for P8 the palette's hash * 31 is
XORed in (tex_hash_pal).

Usage:
  python tools/tex_extract.py OUT_DIR [--area hon_under] [--skip-dir DIR ...] [--list]
    --area      only that area folder (default: every area plus common/ and extras/)
    --skip-dir  do not write textures already present in DIR (e.g. an earlier dump)
    --list      write nothing; print a summary per file
Writes OUT_DIR/<hash16>_<w>x<h>_<fmt>.tga (32-bit BGRA, level 0) and OUT_DIR/index.csv (name, source file, type).
Reads game_files/ (your own copy) only.
"""
import os
import re
import struct
import sys

ROOT = os.path.join(os.path.dirname(__file__), '..', 'game_files', 'romdata')
M64 = (1 << 64) - 1
SEED = 0xCBF29CE484222325
PRIME = 0x100000001B3
TYPES = {5: 0x0B, 8: 0x0C, 12: 0x0F}


def tex_hash(b, h=SEED):
    lanes = [h, h ^ 1, h ^ 2, h ^ 3]
    n, i = len(b), 0
    while i + 32 <= n:
        w = struct.unpack_from('<4Q', b, i)
        for k in range(4):
            lanes[k] = ((lanes[k] ^ w[k]) * PRIME) & M64
        i += 32
    while i < n:
        lanes[0] = ((lanes[0] ^ b[i]) * PRIME) & M64
        i += 1
    return ((lanes[0] ^ (lanes[1] << 1) ^ (lanes[2] << 2) ^ (lanes[3] << 3)) * PRIME) & M64


def pictures(d, base):
    """(width, height, image type, mip levels, image bytes, clut bytes) for each picture of the TIM2 at base."""
    if d[base:base + 4] != b'TIM2':
        return
    n = struct.unpack_from('<H', d, base + 6)[0]
    o = base + (0x80 if d[base + 5] == 1 else 0x10)
    for _ in range(n):
        if o + 0x30 > len(d):
            return
        tot, clut, img, hdr, _cc, _pf, mip, _ct, it, w, h = struct.unpack_from('<IIIHHBBBBHH', d, o)
        if tot == 0 or o + tot > len(d) or hdr < 0x30:
            return
        yield w, h, it, mip, d[o + hdr:o + hdr + img], d[o + hdr + img:o + hdr + img + clut]
        o += tot


def unswizzle_index(x, y, w, h):
    """Xbox (nv2a) swizzle: x and y bits interleaved, x first; the longer side keeps its extra bits on top."""
    o, bit, bx, by = 0, 0, 1, 1
    while bx < w or by < h:
        if bx < w:
            if x & bx:
                o |= 1 << bit
            bit += 1
            bx <<= 1
        if by < h:
            if y & by:
                o |= 1 << bit
            bit += 1
            by <<= 1
    return o


def decode_p8(img, clut, w, h):
    pal = [clut[i * 4:i * 4 + 4] for i in range(len(clut) // 4)]   # stored B, G, R, A
    out = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            idx = img[unswizzle_index(x, y, w, h)]
            p = pal[idx] if idx < len(pal) else b'\0\0\0\0'
            o = (y * w + x) * 4
            out[o:o + 4] = p
    return bytes(out)


def _rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def _color_block(blk, out, w, h, bx, by, dxt1, alpha=None):
    c0, c1, bits = struct.unpack_from('<HHI', blk, 0)
    a, b = _rgb565(c0), _rgb565(c1)
    if c0 > c1 or not dxt1:
        cols = [a + (255,), b + (255,),
                tuple((2 * a[i] + b[i]) // 3 for i in range(3)) + (255,),
                tuple((a[i] + 2 * b[i]) // 3 for i in range(3)) + (255,)]
    else:
        cols = [a + (255,), b + (255,), tuple((a[i] + b[i]) // 2 for i in range(3)) + (255,), (0, 0, 0, 0)]
    for py in range(4):
        for px in range(4):
            x, y = bx * 4 + px, by * 4 + py
            if x >= w or y >= h:
                continue
            k = py * 4 + px
            r, g, bb, al = cols[(bits >> (2 * k)) & 3]
            if alpha is not None:
                al = alpha[k]
            o = (y * w + x) * 4
            out[o:o + 4] = bytes((bb, g, r, al))


def decode_dxt(img, w, h, dxt5):
    out = bytearray(w * h * 4)
    bw, bh, size = (w + 3) // 4, (h + 3) // 4, 16 if dxt5 else 8
    for by in range(bh):
        for bx in range(bw):
            o = (by * bw + bx) * size
            if o + size > len(img):
                return bytes(out)
            if dxt5:
                a0, a1 = img[o], img[o + 1]
                bits = int.from_bytes(img[o + 2:o + 8], 'little')
                if a0 > a1:
                    al = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
                else:
                    al = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
                alpha = [al[(bits >> (3 * k)) & 7] for k in range(16)]
                _color_block(img[o + 8:o + 16], out, w, h, bx, by, False, alpha)
            else:
                _color_block(img[o:o + 8], out, w, h, bx, by, True)
    return bytes(out)


def write_tga(path, w, h, bgra):
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x28)   # top-left origin, 8 alpha bits
    with open(path, 'wb') as f:
        f.write(hdr)
        f.write(bgra)


def level0(img, w, h, fmt):
    if fmt == 0x0B:
        return img[:w * h]
    if fmt == 0x0C:
        return img[:((w + 3) // 4) * ((h + 3) // 4) * 8]
    return img[:((w + 3) // 4) * ((h + 3) // 4) * 16]


def textures(path):
    """(name, w, h, fmt or None, image type, image bytes, clut bytes) for every picture in one file."""
    d = open(path, 'rb').read()
    for m in re.finditer(b'TIM2', d):
        for w, h, it, mip, img, clut in pictures(d, m.start()):
            fmt = TYPES.get(it)
            hsh = tex_hash(img)
            if fmt == 0x0B and clut:
                hsh ^= (tex_hash(clut) * 31) & M64
            name = '%016x_%dx%d_%02X.tga' % (hsh, w, h, fmt if fmt is not None else 0xFF)
            yield name, w, h, fmt, it, img, clut


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    out = a[0]
    area = a[a.index('--area') + 1] if '--area' in a else None
    skip = set()
    for i, x in enumerate(a):
        if x == '--skip-dir' and i + 1 < len(a):
            skip |= {n[:16].lower() for n in os.listdir(a[i + 1])}
    listing = '--list' in a
    folders = [os.path.join(ROOT, 'area', area)] if area else [ROOT]
    files = sorted(os.path.join(r, f) for fo in folders for r, _, fs in os.walk(fo) for f in fs
                   if f.endswith(('.pak', '.nut')) or re.search(r'\.\d{3}$', f))
    if not listing:
        os.makedirs(out, exist_ok=True)
    seen, rows, written, unknown = set(), [], 0, 0
    for path in files:
        rel = os.path.relpath(path, ROOT).replace('\\', '/')
        n_file = n_new = 0
        for name, w, h, fmt, it, img, clut in textures(path):
            n_file += 1
            if name in seen:
                continue
            seen.add(name)
            rows.append('%s,%s,%d' % (name, rel, it))
            if fmt is None:
                unknown += 1
                continue
            if name[:16] in skip:
                continue
            n_new += 1
            if listing:
                continue
            lv = level0(img, w, h, fmt)
            bgra = decode_p8(lv, clut, w, h) if fmt == 0x0B else decode_dxt(lv, w, h, fmt == 0x0F)
            write_tga(os.path.join(out, name), w, h, bgra)
            written += 1
        if listing and n_file:
            print('%-55s %5d pictures, %5d new' % (rel, n_file, n_new))
    if not listing:
        with open(os.path.join(out, 'index.csv'), 'w') as f:
            f.write('name,source,image_type\n' + '\n'.join(rows) + '\n')
    print('%d unique textures, %d written, %d of an unknown type skipped' % (len(seen), written, unknown))
    return 0


if __name__ == '__main__':
    sys.exit(main())
