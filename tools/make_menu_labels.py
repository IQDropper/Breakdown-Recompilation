"""make_menu_labels.py - build menu_labels.pack (a texture pack) from YOUR game files.

The main-menu Options list has a cut 5th row whose label reads SAVE SETTINGS. RECOMP_VIDEO_ROW=1 turns that row
into VIDEO SETTINGS (src/recomp_manual.c); this script makes its label match. It reads the options label sheet
(E_Opt_Sw_Tex, 256x256 DXT5) from romdata/common/r_BD00*.ast, rebuilds the SAVE SETTINGS strip as VIDEO SETTINGS
from glyphs cut out of the same sheet (same font, same glow), and writes a one-entry XRTP v2 pack keyed by the
hash the GPU backend computes over the texture's guest bytes (nv2a_tex_hash over the DXT5 data). The game and
its files are untouched; nothing produced here is committed (it is derived from the game).

Usage:  python tools/make_menu_labels.py [game_files dir] [output pack]
        defaults: game_files  and  build-win/menu_labels.pack
Run the game with RECOMP_TEXTURES_PACK=menu_labels.pack (relative to the exe's folder)."""
import os
import re
import struct
import sys

SEED = 0xCBF29CE484222325
PRIME = 0x100000001B3
M64 = (1 << 64) - 1
SHEET = "E_Opt_Sw_Tex"

# Glyph boxes in the 256x256 sheet, found from the alpha (see the kit's reports/research/game-menus-notes.md).
# Each text row is a 12-pixel band; letters are (x0, x1) columns inside a band starting at band_y.
SAVE_BAND = 60                       # "SAVE SETTINGS"
SRC = {                              # letter: (band_y, x0, x1)
    "V": (60, 33, 47), "E": (60, 49, 63), "I": (60, 150, 158),
    "D": (36, 16, 30),               # from ADJUST SCREEN
    "O": (0, 89, 103),               # from GAME OPTIONS
    "S": (60, 82, 96), "T": (60, 114, 128), "N": (60, 163, 177), "G": (60, 179, 194),
}
LETTER_GAP = 2
WORD_GAP = 12
BAND_H = 12


def tex_hash(data):
    lanes = [SEED, SEED ^ 1, SEED ^ 2, SEED ^ 3]
    n = len(data)
    i = 0
    while i + 32 <= n:
        w = struct.unpack_from("<4Q", data, i)
        for k in range(4):
            lanes[k] = ((lanes[k] ^ w[k]) * PRIME) & M64
        i += 32
    while i < n:
        lanes[0] = ((lanes[0] ^ data[i]) * PRIME) & M64
        i += 1
    x = lanes[0] ^ ((lanes[1] << 1) & M64) ^ ((lanes[2] << 2) & M64) ^ ((lanes[3] << 3) & M64)
    return (x * PRIME) & M64


def ast_sheet(path):
    """The DXT5 bytes of E_Opt_Sw_Tex in an ASPT (.ast) file: a name table, then one TIM2 per name, in order."""
    d = open(path, "rb").read()
    if d[:4] != b"ASPT":
        raise ValueError("not an ASPT file")
    n = struct.unpack_from("<I", d, 4)[0]
    names = [d[o:d.index(b"\0", o)].decode("latin1") for o in struct.unpack_from("<%dI" % n, d, 8)]
    tims = [m.start() for m in re.finditer(b"TIM2", d)]
    if len(tims) != n or SHEET not in names:
        raise ValueError("unexpected layout (%d names, %d TIM2)" % (n, len(tims)))
    t = tims[names.index(SHEET)]
    o = t + (0x80 if d[t + 5] == 1 else 0x10)
    tot, clut, img, hdr, _ = struct.unpack_from("<IIIHH", d, o)
    _, mips, _, kind, w, h = struct.unpack_from("<BBBBHH", d, o + 0x10)
    if (kind, w, h, img) != (12, 256, 256, 65536):
        raise ValueError("sheet is not 256x256 DXT5 (type %d %dx%d)" % (kind, w, h))
    return d[o + hdr:o + hdr + img]


def rgb565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def dxt5_decode(data, w, h):
    px = bytearray(w * h * 4)                       # RGBA
    bi = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            a0, a1 = data[bi], data[bi + 1]
            abits = int.from_bytes(data[bi + 2:bi + 8], "little")
            c0, c1, cbits = struct.unpack_from("<HHI", data, bi + 8)
            bi += 16
            al = [a0, a1] + ([((6 - k) * a0 + (1 + k) * a1) // 7 for k in range(6)] if a0 > a1
                             else [((4 - k) * a0 + (1 + k) * a1) // 5 for k in range(4)] + [0, 255])
            p0, p1 = rgb565(c0), rgb565(c1)
            cols = [p0, p1, tuple((2 * a + b) // 3 for a, b in zip(p0, p1)),
                    tuple((a + 2 * b) // 3 for a, b in zip(p0, p1))]
            for i in range(16):
                x, y = bx + (i & 3), by + (i >> 2)
                r, g, b = cols[(cbits >> (2 * i)) & 3]
                o = (y * w + x) * 4
                px[o:o + 4] = bytes((r, g, b, al[(abits >> (3 * i)) & 7]))
    return px


def build_label(px, w=256):
    """Clear the SAVE SETTINGS band and lay VIDEO SETTINGS into it from the cut-out glyphs."""
    src = bytes(px)

    def glyph(ch):
        y0, x0, x1 = SRC[ch]
        # Trim to the letter's solid columns (+1 for the glow): a box can catch a neighbour's faint serif.
        solid = [x for x in range(x0, x1) if any(src[((y0 + r) * w + x) * 4 + 3] > 128 for r in range(BAND_H))]
        x0, x1 = max(x0, solid[0] - 1), min(x1, solid[-1] + 2)
        return [src[((y0 + r) * w + x0) * 4:((y0 + r) * w + x1) * 4] for r in range(BAND_H)], x1 - x0

    for r in range(BAND_H):
        o = (SAVE_BAND + r) * w * 4
        px[o:o + w * 4] = bytes(w * 4)
    x = 0
    for wi, word in enumerate(("VIDEO", "SETTINGS")):
        if wi:
            x += WORD_GAP - LETTER_GAP
        for ch in word:
            rows, gw = glyph(ch)
            if x + gw > w:
                raise ValueError("label does not fit")
            for r, row in enumerate(rows):
                o = ((SAVE_BAND + r) * w + x) * 4
                px[o:o + gw * 4] = row
            x += gw + LETTER_GAP
    return x - LETTER_GAP


def write_pack(path, entries):
    """XRTP v2: header, sorted 32-byte index, A8R8G8B8 payloads (B, G, R, A bytes). Kind 2 = baked text."""
    entries = sorted(entries)
    hdr = 32
    off = hdr + 32 * len(entries)
    index, blobs = b"", b""
    for hsh, w, h, rgba in entries:
        bgra = bytearray(rgba)
        bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
        index += struct.pack("<QIIIIQ", hsh, w, h, 1, 2, off + len(blobs))
        blobs += bytes(bgra)
    head = b"XRTP" + struct.pack("<IIIQII", 2, len(entries), 1, hdr, 1, 0)
    with open(path, "wb") as f:
        f.write(head + index + blobs)


def main():
    files = sys.argv[1] if len(sys.argv) > 1 else "game_files"
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join("build-win", "menu_labels.pack")
    entries, seen = [], set()
    for name in ("r_BD00_USA.ast", "r_BD00.ast"):
        p = os.path.join(files, "romdata", "common", name)
        if not os.path.exists(p):
            continue
        dxt = ast_sheet(p)
        hsh = tex_hash(dxt)
        if hsh in seen:
            continue
        seen.add(hsh)
        px = dxt5_decode(dxt, 256, 256)
        end = build_label(px)
        entries.append((hsh, 256, 256, bytes(px)))
        print("%s: sheet %016X, VIDEO SETTINGS laid out to x=%d" % (name, hsh, end))
    if not entries:
        print("[error] no r_BD00*.ast under %s/romdata/common" % files)
        return 1
    write_pack(out, entries)
    print("wrote %s (%d entries). Run with RECOMP_TEXTURES_PACK=%s" % (out, len(entries), os.path.basename(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
