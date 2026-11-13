"""tools/make_tex_pack.py: images named by hash -> XRTP v2 pack (TGA, PNG, DDS; mips; --include)."""

import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import make_tex_pack as mtp  # noqa: E402


def write_tga(path, w, h, rgba):
    hdr = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x28)
    body = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        body += bytes((b, g, r, a))
    Path(path).write_bytes(hdr + bytes(body))


def write_png(path, w, h, rgba):
    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


class MakeTexPack(unittest.TestCase):
    def test_tga_png_mips_and_include(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            src = d / "img"
            src.mkdir()
            px = bytes((255, 0, 0, 128)) * 16   # 4x4 red, half alpha
            write_tga(src / "00000000000000AA_4x4_06.tga", 4, 4, px)
            write_png(src / "00000000000000BB_4x4_06.png", 4, 4, bytes((0, 255, 0, 255)) * 16)
            (src / "notes.txt").write_text("ignored")
            out = d / "a.pack"
            self.assertEqual(mtp.main([str(src), str(out)]), 0)
            ents = {e[0]: e for e in mtp.read_pack(str(out))}
            self.assertEqual(sorted(ents), [0xAA, 0xBB])
            h, w, hh, lv, fl, payload = ents[0xAA]
            self.assertEqual((w, hh, lv, fl), (4, 4, 3, 0))           # 4x4, 2x2, 1x1
            self.assertEqual(len(payload), (16 + 4 + 1) * 4)
            self.assertEqual(payload[:4], bytes((0, 0, 255, 128)))    # BGRA, alpha kept
            self.assertEqual(ents[0xBB][5][:4], bytes((0, 255, 0, 255)))
            # --include keeps another pack's entries; --no-mips; --kind text
            src2 = d / "img2"
            src2.mkdir()
            write_tga(src2 / "00000000000000CC.tga", 2, 1, bytes((1, 2, 3, 4)) * 2)
            out2 = d / "b.pack"
            self.assertEqual(mtp.main([str(src2), str(out2), "--include", str(out), "--no-mips", "--kind", "text"]), 0)
            ents2 = {e[0]: e for e in mtp.read_pack(str(out2))}
            self.assertEqual(sorted(ents2), [0xAA, 0xBB, 0xCC])
            self.assertEqual(ents2[0xCC][3:5], (1, 2))
            self.assertEqual(ents2[0xAA][5], ents[0xAA][5])            # copied unchanged

    def test_empty_folder_fails(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(mtp.main([d, str(Path(d) / "x.pack")]), 1)


if __name__ == "__main__":
    unittest.main()
