"""tex_review_sheets.py <dump dir> <upscaled dir> <out dir> [--per 20] - contact sheets for reviewing an upscaled pack.

Each tile: the original (enlarged with nearest, so its real pixels show) next to the upscaled version, both 256 px
on the long side, on a grey checker (transparency shows), labelled with the texture hash and size. Use it to find
textures where the upscaler invented detail, e.g. writing that was too small to read in the original.
Standard library only (reads TGA/PNG with tools/make_tex_pack.py next to it)."""
import argparse
import os
import sys

TILE = 256
GAP = 8
LABEL = 14   # rows reserved for the label strip (drawn as a dark bar; the hash is in sheet_index.txt)


def load_tools(repo):
    sys.path.insert(0, os.path.join(repo, "tools"))
    import make_tex_pack as m  # noqa
    import upscale_tex as u  # noqa
    return m, u


def fit(w, h):
    s = TILE / max(w, h)
    return max(1, int(w * s)), max(1, int(h * s))


def resample(w, h, px, W, H):
    out = bytearray(W * H * 4)
    for y in range(H):
        sy = min(h - 1, y * h // H)
        for x in range(W):
            sx = min(w - 1, x * w // W)
            i = (sy * w + sx) * 4
            o = (y * W + x) * 4
            out[o:o + 4] = px[i:i + 4]
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("up")
    ap.add_argument("out")
    ap.add_argument("--per", type=int, default=20)
    ap.add_argument("--repo", default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    a = ap.parse_args(argv)
    m, u = load_tools(a.repo)
    os.makedirs(a.out, exist_ok=True)
    ups = {n[:16].upper(): n for n in os.listdir(a.up) if n.lower().endswith(".png")}
    pairs = []
    for n in sorted(os.listdir(a.dump)):
        h = n[:16].upper()
        if h in ups and n.lower().endswith(".tga"):
            pairs.append((n, ups[h]))
    cols = 4
    index = []
    for s0 in range(0, len(pairs), a.per):
        chunk = pairs[s0:s0 + a.per]
        rows = (len(chunk) + cols - 1) // cols
        pw = 2 * TILE + GAP
        SW = cols * pw + (cols - 1) * 3 * GAP
        SH = rows * (TILE + LABEL + 2 * GAP)
        sheet = bytearray(SW * SH * 4)
        for i in range(SW * SH):
            sheet[i * 4:i * 4 + 4] = b"\x20\x20\x20\xff"
        sheet_no = s0 // a.per + 1
        for k, (orig, upn) in enumerate(chunk):
            w, h, o = m.read_tga(os.path.join(a.dump, orig))
            W, H, b = m.read_png(os.path.join(a.up, upn))
            tw, th = fit(w, h)
            ta = resample(w, h, o, tw, th)
            tb = resample(W, H, b, tw, th)
            cx = (k % cols) * (pw + 3 * GAP)
            cy = (k // cols) * (TILE + LABEL + 2 * GAP)
            for img, ox in ((ta, cx), (tb, cx + TILE + GAP)):
                for y in range(th):
                    for x in range(tw):
                        r, g, bb, al = img[(y * tw + x) * 4:(y * tw + x) * 4 + 4]
                        c = 110 if ((x // 8 + y // 8) % 2) else 70
                        kk = al / 255.0
                        p = ((cy + LABEL + y) * SW + ox + x) * 4
                        sheet[p:p + 4] = bytes((int(r * kk + c * (1 - kk)), int(g * kk + c * (1 - kk)),
                                                int(bb * kk + c * (1 - kk)), 255))
            index.append("sheet %d tile %2d (row %d, col %d): %s  %dx%d" % (sheet_no, k + 1, k // cols + 1, k % cols + 1,
                                                                           orig[:16].upper(), w, h))
        u.write_png(os.path.join(a.out, "sheet_%02d.png" % sheet_no), SW, SH, bytes(sheet))
    open(os.path.join(a.out, "sheet_index.txt"), "w").write("\n".join(index) + "\n")
    print("%d pairs, %d sheets -> %s" % (len(pairs), (len(pairs) + a.per - 1) // a.per, a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
