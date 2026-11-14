"""Checks for tools/tex_extract.py that need no game files."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))
import tex_extract as t  # noqa: E402


class TexExtract(unittest.TestCase):
    def test_hash_empty_is_seed_mix(self):
        h, p, m = t.SEED, t.PRIME, t.M64
        want = ((h ^ ((h ^ 1) << 1) ^ ((h ^ 2) << 2) ^ ((h ^ 3) << 3)) * p) & m
        self.assertEqual(t.tex_hash(b''), want)

    def test_hash_changes_with_one_byte(self):
        a = bytes(64)
        b = bytes(63) + b'\x01'
        self.assertNotEqual(t.tex_hash(a), t.tex_hash(b))

    def test_unswizzle_square(self):
        # 4x4: x bit0 -> bit0, y bit0 -> bit1, x bit1 -> bit2, y bit1 -> bit3
        self.assertEqual(t.unswizzle_index(1, 0, 4, 4), 1)
        self.assertEqual(t.unswizzle_index(0, 1, 4, 4), 2)
        self.assertEqual(t.unswizzle_index(2, 0, 4, 4), 4)
        self.assertEqual(t.unswizzle_index(3, 3, 4, 4), 15)

    def test_unswizzle_wide_keeps_extra_x_bits_on_top(self):
        # 8x2: x0 y0 x1 x2
        self.assertEqual(t.unswizzle_index(0, 1, 8, 2), 2)
        self.assertEqual(t.unswizzle_index(4, 0, 8, 2), 8)
        idx = sorted(t.unswizzle_index(x, y, 8, 2) for x in range(8) for y in range(2))
        self.assertEqual(idx, list(range(16)))

    def test_p8_uses_palette_bgra(self):
        clut = bytes((10, 20, 30, 255)) + bytes((1, 2, 3, 4))
        img = bytes((1, 0, 0, 0))   # 2x2, swizzled order (0,0) (1,0) (0,1) (1,1)
        out = t.decode_p8(img, clut, 2, 2)
        self.assertEqual(out[0:4], bytes((1, 2, 3, 4)))
        self.assertEqual(out[4:8], bytes((10, 20, 30, 255)))

    def test_dxt1_solid_red(self):
        blk = struct.pack('<HHI', 0xF800, 0xF800, 0)   # both endpoints pure red, all texels index 0
        out = t.decode_dxt(blk, 4, 4, False)
        self.assertEqual(out[0:4], bytes((0, 0, 255, 255)))
        self.assertEqual(len(out), 64)

    def test_dxt5_alpha_endpoints(self):
        alpha = bytes((255, 0)) + bytes(6)              # every texel uses a0 = 255
        color = struct.pack('<HHI', 0x07E0, 0x07E0, 0)  # pure green
        out = t.decode_dxt(alpha + color, 4, 4, True)
        self.assertEqual(out[0:4], bytes((0, 255, 0, 255)))

    def test_level0_sizes(self):
        self.assertEqual(len(t.level0(bytes(100), 8, 8, 0x0B)), 64)
        self.assertEqual(len(t.level0(bytes(100), 8, 8, 0x0C)), 32)
        self.assertEqual(len(t.level0(bytes(100), 8, 8, 0x0F)), 64)


if __name__ == '__main__':
    unittest.main()
