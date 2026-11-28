"""pack_replace.py - swap one texture inside an existing XRTP pack, in place, without rebuilding the pack.

python tools/pack_replace.py <pack> <image.png|.tga> [<image> ...]
Each image is named <hash16>_... like make_tex_pack.py inputs. The pack must already hold that hash at the same size,
uncompressed (not BC3), with a full mip chain or one level: the new level 0 and its box-filtered mips are written over
the old payload, so the pack keeps its size and every other entry. Close the game first (it reads the pack).
Also: --get <hash16> <out.png> writes level 0 of an entry as a PNG (to edit it, or keep a copy before replacing)."""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_tex_pack import HASH_RE, mip_chain, read_pack_index, read_png, read_tga, to_bgra  # noqa: E402


def write_png(path, w, h, bgra):
    raw = bytearray()
    for y in range(h):
        r = bytearray(bgra[y * w * 4:(y + 1) * w * 4])
        r[0::4], r[2::4] = r[2::4], r[0::4]
        raw += b"\0" + r

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def main(argv):
    if len(argv) == 4 and argv[1] == "--get":
        pack, want, out = argv[0], int(argv[2], 16), argv[3]
        for hsh, w, h, lv, fl, off, size in read_pack_index(pack):
            if hsh == want:
                if fl & 0x10:
                    sys.exit("entry is BC3")
                with open(pack, "rb") as f:
                    f.seek(off)
                    write_png(out, w, h, f.read(w * h * 4))
                print("%016X %dx%d -> %s" % (hsh, w, h, out))
                return
        sys.exit("%016X not in %s" % (want, pack))
    if len(argv) < 2:
        sys.exit(__doc__)
    pack = argv[0]
    idx = {r[0]: r for r in read_pack_index(pack)}
    with open(pack, "r+b") as f:
        for path in argv[1:]:
            m = HASH_RE.match(os.path.basename(path))
            if not m:
                sys.exit("%s: name must start with the 16-digit hash" % path)
            hsh = int(m.group(1), 16)
            if hsh not in idx:
                sys.exit("%016X not in %s" % (hsh, pack))
            _, w, h, lv, fl, off, size = idx[hsh]
            iw, ih, rgba = read_tga(path) if path.lower().endswith(".tga") else read_png(path)
            if (iw, ih) != (w, h) or fl & 0x10:
                sys.exit("%s: pack entry is %dx%d flags %x, image %dx%d" % (path, w, h, fl, iw, ih))
            levels, payload = mip_chain(w, h, rgba) if lv > 1 else (1, to_bgra(rgba))
            if levels != lv or len(payload) > size:
                sys.exit("%s: payload does not fit (%d levels/%d bytes vs %d/%d)" % (path, levels, len(payload), lv, size))
            f.seek(off)
            f.write(payload)
            print("%016X %dx%d replaced in %s" % (hsh, w, h, pack))


if __name__ == "__main__":
    main(sys.argv[1:])
