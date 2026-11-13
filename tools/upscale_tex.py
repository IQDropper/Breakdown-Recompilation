"""upscale_tex.py - upscale dumped textures for a texture pack (S6.3).

1. Dump the game's textures: run with RECOMP_TEX_DUMP=<folder> (see tools/make_tex_pack.py).
2. python tools/upscale_tex.py <dump folder> <out folder> [--scale 4] [--edge clamp|wrap] [--only HASH ...]
                               [--external "command {in} {out}"] [--jobs N]
3. python tools/make_tex_pack.py <out folder> <out.pack>, then run with RECOMP_TEXTURES_PACK=<out.pack>.

Each image keeps its hash (the first 16 hex digits of its name) and gets the new size in its name, written as PNG.
Default filter: Lanczos-3 on premultiplied alpha (no dark fringes at cut-out edges). Standard library only; if
Pillow happens to be installed it is used for the same filter (much faster). It uses every CPU core.
--edge wrap treats the texture as tiling (walls, floors); clamp (default) suits cut-outs, UI and atlases.
--external runs your own upscaler per image instead (for example an ESRGAN build you installed yourself):
{in} is a PNG of the original, {out} the PNG it must write. Nothing is downloaded by this script.
The images and packs come from your own game files: never commit them."""
import argparse
import math
import os
import shlex
import struct
import subprocess
import sys
import tempfile
import zlib
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_tex_pack as mtp  # noqa: E402  (read_tga / read_png / HASH_RE)

READERS = {".tga": mtp.read_tga, ".png": mtp.read_png}   # dumps are TGA; DDS is a replacement format only


def write_png(path, w, h, rgba):
    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def lanczos(x, a=3):
    if x == 0:
        return 1.0
    if -a < x < a:
        px = math.pi * x
        return a * math.sin(px) * math.sin(px / a) / (px * px)
    return 0.0


def weights(n_in, n_out, wrap, a=3):
    """Per output pixel: list of (input index, weight), normalised. Upscale only (n_out >= n_in)."""
    out = []
    ratio = n_in / n_out
    for o in range(n_out):
        c = (o + 0.5) * ratio - 0.5
        lo = int(math.floor(c)) - a + 1
        taps = []
        for i in range(lo, lo + 2 * a):
            wgt = lanczos(c - i, a)
            if wgt == 0.0:
                continue
            j = i % n_in if wrap else min(max(i, 0), n_in - 1)
            taps.append((j, wgt))
        s = sum(t[1] for t in taps) or 1.0
        out.append([(j, wgt / s) for j, wgt in taps])
    return out


def upscale_rgba(w, h, rgba, scale, wrap=False):
    """Lanczos-3 upscale of 8-bit RGBA by an integer scale, on premultiplied alpha. Returns (W, H, bytes)."""
    W, H = w * scale, h * scale
    # premultiplied floats, one list per channel
    px = [[0.0] * (w * h) for _ in range(4)]
    for i in range(w * h):
        r, g, b, al = rgba[i * 4:i * 4 + 4]
        k = al / 255.0
        px[0][i], px[1][i], px[2][i], px[3][i] = r * k, g * k, b * k, float(al)
    wx = weights(w, W, wrap)
    wy = weights(h, H, wrap)
    out = bytearray(W * H * 4)
    for ch in range(4):
        src = px[ch]
        # horizontal pass: h rows x W
        tmp = [0.0] * (W * h)
        for y in range(h):
            row = src[y * w:(y + 1) * w]
            base = y * W
            for x, taps in enumerate(wx):
                tmp[base + x] = sum(row[j] * t for j, t in taps)
        # vertical pass: H x W
        dst = [0.0] * (W * H)
        for y, taps in enumerate(wy):
            base = y * W
            rows = [(j * W, t) for j, t in taps]
            for x in range(W):
                dst[base + x] = sum(tmp[o + x] * t for o, t in rows)
        px[ch] = dst
    for i in range(W * H):
        a = px[3][i]
        a = 0.0 if a < 0.0 else 255.0 if a > 255.0 else a
        out[i * 4 + 3] = int(a + 0.5)
        for ch in range(3):
            v = px[ch][i] * 255.0 / a if a > 0.5 else 0.0
            out[i * 4 + ch] = 0 if v < 0.0 else 255 if v > 255.0 else int(v + 0.5)
    return W, H, bytes(out)


def upscale_pillow(w, h, rgba, scale, wrap):
    from PIL import Image  # only if installed; never installed by this script
    img = Image.frombytes("RGBA", (w, h), rgba)
    if wrap:   # tile 3x3, filter, cut the middle: wrap edges like the pure path
        big = Image.new("RGBA", (w * 3, h * 3))
        for ty in range(3):
            for tx in range(3):
                big.paste(img, (tx * w, ty * h))
        big = big.convert("RGBa").resize((w * 3 * scale, h * 3 * scale), Image.LANCZOS).convert("RGBA")
        out = big.crop((w * scale, h * scale, 2 * w * scale, 2 * h * scale))
    else:
        out = img.convert("RGBa").resize((w * scale, h * scale), Image.LANCZOS).convert("RGBA")
    return w * scale, h * scale, out.tobytes()


def have_pillow():
    try:
        import PIL  # noqa: F401
        return True
    except ImportError:
        return False


def out_name(name, W, H):
    m = mtp.HASH_RE.match(name)
    return "%s_%dx%d_up.png" % (m.group(1).upper(), W, H)


def work(job):
    path, out_dir, scale, wrap, external, use_pil = job
    name = os.path.basename(path)
    ext = os.path.splitext(name)[1].lower()
    try:
        w, h, rgba = READERS[ext](path)
        if external:
            with tempfile.TemporaryDirectory() as td:
                src = os.path.join(td, "in.png")
                dst = os.path.join(td, "out.png")
                write_png(src, w, h, rgba)
                cmd = external.replace("{in}", '"%s"' % src).replace("{out}", '"%s"' % dst)
                r = subprocess.run(cmd if os.name == "nt" else shlex.split(cmd), shell=(os.name == "nt"),
                                   capture_output=True, text=True)
                if r.returncode != 0 or not os.path.exists(dst):
                    raise RuntimeError("external upscaler failed (%d): %s" % (r.returncode, (r.stderr or "").strip()[:200]))
                W, H, big = mtp.read_png(dst)
        elif use_pil:
            W, H, big = upscale_pillow(w, h, rgba, scale, wrap)
        else:
            W, H, big = upscale_rgba(w, h, rgba, scale, wrap)
        out = os.path.join(out_dir, out_name(name, W, H))
        write_png(out, W, H, big)
        return name, "%dx%d -> %dx%d" % (w, h, W, H), None
    except Exception as e:  # report and go on with the others
        return name, None, str(e)


def main(argv):
    ap = argparse.ArgumentParser(description="Upscale dumped textures (Lanczos-3), keeping their hash names.")
    ap.add_argument("src")
    ap.add_argument("out")
    ap.add_argument("--scale", type=int, default=4, choices=(2, 3, 4))
    ap.add_argument("--edge", choices=("clamp", "wrap"), default="clamp")
    ap.add_argument("--only", nargs="*", default=None, help="hashes (16 hex digits) to do; default all")
    ap.add_argument("--external", default=None, help='your own upscaler: "command {in} {out}"')
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    a = ap.parse_args(argv)
    if a.external and ("{in}" not in a.external or "{out}" not in a.external):
        print("--external needs {in} and {out} in the command", file=sys.stderr)
        return 2
    os.makedirs(a.out, exist_ok=True)
    only = {h.upper() for h in a.only} if a.only else None
    files = []
    for n in sorted(os.listdir(a.src)):
        m = mtp.HASH_RE.match(n)
        if not m or os.path.splitext(n)[1].lower() not in READERS:
            continue
        if only and m.group(1).upper() not in only:
            continue
        files.append(os.path.join(a.src, n))
    if not files:
        print("no images named <16 hex digits>... in %s" % a.src, file=sys.stderr)
        return 1
    use_pil = not a.external and have_pillow()
    how = "external: " + a.external if a.external else ("Pillow Lanczos" if use_pil else "Lanczos-3 (standard library)")
    print("%d images, x%d, edge %s, %s, %d jobs" % (len(files), a.scale, a.edge, how, a.jobs))
    jobs = [(f, a.out, a.scale, a.edge == "wrap", a.external, use_pil) for f in files]
    bad = 0
    if a.jobs > 1 and len(jobs) > 1:
        with ProcessPoolExecutor(max_workers=a.jobs) as ex:
            results = list(ex.map(work, jobs))
    else:
        results = [work(j) for j in jobs]
    for name, ok, err in results:
        if err:
            bad += 1
            print("  FAIL %s: %s" % (name, err))
        else:
            print("  %s  %s" % (name, ok))
    print("done: %d written, %d failed -> %s" % (len(results) - bad, bad, a.out))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
