"""callers.py <xbe> <va> [...] : list direct `call rel32` (E8) and `push imm32` (68, e.g. vtable/callback) sites of each VA."""
import sys, struct
X = open(sys.argv[1], 'rb').read()
base = struct.unpack_from('<I', X, 0x104)[0]
n = struct.unpack_from('<I', X, 0x11C)[0]
sh = struct.unpack_from('<I', X, 0x120)[0] - base
secs = [struct.unpack_from('<6I', X, sh + i*0x38)[1:5] for i in range(n)]
def off2va(o):
    for va, vs, ra, rs in secs:
        if ra <= o < ra + rs: return va + o - ra
for a in sys.argv[2:]:
    t = int(a, 16); out = []
    for va, vs, ra, rs in secs:
        for o in range(ra, ra + rs - 5):
            if X[o] == 0xE8 and va + (o - ra) + 5 + struct.unpack_from('<i', X, o + 1)[0] == t:
                out.append(f'call@0x{va + o - ra:X}')
    p = 0; pat = struct.pack('<I', t)
    while (p := X.find(pat, p + 1)) > 0:
        v = off2va(p)
        if v: out.append(f'ptr@0x{v:X}(op {X[p-1]:02X})')
    print(f'0x{t:X}: ' + ' '.join(out[:20]))
