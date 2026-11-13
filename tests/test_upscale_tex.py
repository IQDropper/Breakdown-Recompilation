"""tools/upscale_tex.py: Lanczos upscale of dumped textures, keeping hash names (S6.3)."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import make_tex_pack as mtp  # noqa: E402
import upscale_tex as up  # noqa: E402

HASH = "0123456789ABCDEF"


def write_tga(path, w, h, rgba):
    hdr = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x28)
    body = bytearray()
    for i in range(0, len(rgba), 4):
        r, g, b, a = rgba[i:i + 4]
        body += bytes((b, g, r, a))
    Path(path).write_bytes(hdr + bytes(body))


class UpscaleTest(unittest.TestCase):
    def test_flat_colour_stays_flat(self):
        rgba = bytes((200, 100, 50, 255)) * 16
        W, H, out = up.upscale_rgba(4, 4, rgba, 4)
        self.assertEqual((W, H), (16, 16))
        self.assertEqual(set(out[i:i + 4] for i in range(0, len(out), 4)), {bytes((200, 100, 50, 255))})

    def test_no_dark_fringe_at_alpha_edge(self):
        # left half opaque red, right half fully transparent black: the red must stay red where alpha > 0
        rgba = b"".join(bytes((255, 0, 0, 255)) if x < 2 else bytes((0, 0, 0, 0)) for y in range(4) for x in range(4))
        W, H, out = up.upscale_rgba(4, 4, rgba, 4)
        for i in range(0, len(out), 4):
            r, g, b, a = out[i:i + 4]
            if a > 8:
                self.assertGreater(r, 240, "dark fringe at pixel %d: %r" % (i // 4, out[i:i + 4]))
                self.assertLess(max(g, b), 16)

    def test_wrap_vs_clamp_at_edges(self):
        # a 4x1 strip: white, black, black, black. With wrap, the right edge sees the white again.
        rgba = bytes((255, 255, 255, 255)) + bytes((0, 0, 0, 255)) * 3
        _, _, clamp = up.upscale_rgba(4, 1, rgba, 4, wrap=False)
        _, _, wrap = up.upscale_rgba(4, 1, rgba, 4, wrap=True)
        last = (16 - 1) * 4
        self.assertGreater(wrap[last], clamp[last])

    def test_folder_run_keeps_hash_and_feeds_the_pack(self):
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, "dump"), Path(td, "up")
            src.mkdir()
            rgba = bytes((x * 30 % 256, y * 30 % 256, 128, 255)[c] for y in range(8) for x in range(8) for c in range(4))
            write_tga(src / (HASH + "_8x8_A8R8G8B8.tga"), 8, 8, rgba)
            (src / "notes.txt").write_text("not an image")
            self.assertEqual(up.main([str(src), str(out), "--scale", "2", "--jobs", "1"]), 0)
            files = sorted(p.name for p in out.iterdir())
            self.assertEqual(files, [HASH + "_16x16_up.png"])
            w, h, big = mtp.read_png(str(out / files[0]))
            self.assertEqual((w, h, len(big)), (16, 16, 16 * 16 * 4))
            self.assertEqual(mtp.main([str(out), str(Path(td, "t.pack"))]), 0)

    def test_external_hook(self):
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, "dump"), Path(td, "up")
            src.mkdir()
            write_tga(src / (HASH + "_4x4_A8R8G8B8.tga"), 4, 4, bytes((9, 9, 9, 255)) * 16)
            copier = Path(td, "copy2x.py")
            copier.write_text(
                "import sys\nsys.path.insert(0, %r)\nimport upscale_tex as u, make_tex_pack as m\n"
                "w, h, d = m.read_png(sys.argv[1])\nW, H, b = u.upscale_rgba(w, h, d, 2)\nu.write_png(sys.argv[2], W, H, b)\n"
                % str(ROOT / "tools"))
            cmd = '"%s" "%s" {in} {out}' % (sys.executable, copier)
            self.assertEqual(up.main([str(src), str(out), "--external", cmd, "--jobs", "1"]), 0)
            self.assertEqual([p.name for p in out.iterdir()], [HASH + "_8x8_up.png"])

    def test_external_batch_and_max_side(self):
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, "dump"), Path(td, "up")
            src.mkdir()
            write_tga(src / (HASH + "_4x4_A8R8G8B8.tga"), 4, 4, bytes((9, 9, 9, 255)) * 16)
            write_tga(src / ("FEDCBA9876543210" + "_8x8_A8R8G8B8.tga"), 8, 8, bytes((1, 2, 3, 255)) * 64)
            batch = Path(td, "batch2x.py")
            batch.write_text(
                "import os, sys\nsys.path.insert(0, %r)\nimport upscale_tex as u, make_tex_pack as m\n"
                "for n in os.listdir(sys.argv[1]):\n"
                "    w, h, d = m.read_png(os.path.join(sys.argv[1], n))\n"
                "    W, H, b = u.upscale_rgba(w, h, d, 2)\n"
                "    u.write_png(os.path.join(sys.argv[2], n), W, H, b)\n" % str(ROOT / "tools"))
            cmd = '"%s" "%s" {in_dir} {out_dir}' % (sys.executable, batch)
            self.assertEqual(up.main([str(src), str(out), "--external-batch", cmd, "--max-side", "4"]), 0)
            self.assertEqual([p.name for p in out.iterdir()], [HASH + "_8x8_up.png"])   # the 8x8 one was skipped

    def test_linear_textures_are_skipped(self):
        self.assertTrue(up.is_linear("726fc122dda6e3f3_128x64_13.tga"))
        self.assertTrue(up.is_linear("02336a2704a1eaa2_640x480_24.tga"))
        self.assertFalse(up.is_linear("783e685be9afaa32_256x256_0C.tga"))
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, "dump"), Path(td, "up")
            src.mkdir()
            write_tga(src / (HASH + "_4x4_13.tga"), 4, 4, bytes((9, 9, 9, 255)) * 16)
            self.assertEqual(up.main([str(src), str(out), "--jobs", "1"]), 1)   # nothing left to do
            self.assertEqual(list(out.iterdir()), [])

    def test_external_needs_placeholders(self):
        with tempfile.TemporaryDirectory() as td:
            self.assertEqual(up.main([td, td, "--external", "esrgan.exe"]), 2)
            self.assertEqual(up.main([td, td, "--external-batch", "esrgan.exe -i x"]), 2)


if __name__ == "__main__":
    unittest.main()
