"""xref.py <xbe> <string> [...] : find string VA, then code sites that push/mov that VA.
Prints: string VA, each referencing site VA, and the next CALL target after the site."""
import sys, struct
X = open(sys.argv[1], 'rb').read()
base = struct.unpack_from('<I', X, 0x104)[0]
n = struct.unpack_from('<I', X, 0x11C)[0]
sh = struct.unpack_from('<I', X, 0x120)[0] - base
secs = []
for i in range(n):
    fl, va, vs, ra, rs, na = struct.unpack_from('<6I', X, sh + i*0x38)
    name = X[na-base:X.index(b'\0', na-base)].decode()
    secs.append((name, va, vs, ra, rs))
def off2va(o):
    for nm, va, vs, ra, rs in secs:
        if ra <= o < ra+rs: return va + o - ra
def va2off(v):
    for nm, va, vs, ra, rs in secs:
        if va <= v < va+rs: return ra + v - va
for s in sys.argv[2:]:
    o = X.find(s.encode())
    if o < 0: print(s, 'not found'); continue
    va = off2va(o)
    print(f'{s!r}: VA 0x{va:X}')
    pat = struct.pack('<I', va); p = 0
    while (p := X.find(pat, p+1)) > 0:
        site = off2va(p)
        if site is None: continue
        # next call e8 within 40 bytes
        tgt = ''
        for k in range(p+4, p+60):
            if X[k] == 0xE8:
                rel = struct.unpack_from('<i', X, k+1)[0]
                tgt = f'call 0x{off2va(k)+5+rel:X} @0x{off2va(k):X}'; break
        print(f'   ref @0x{site:X} (op {X[p-1]:02X})  next {tgt}')
