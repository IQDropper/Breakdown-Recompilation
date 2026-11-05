"""audit_stubs.py - which 'unresolved' stubs are really tail blocks of the function just before them?
A stub in src/recomp/gen/recomp_stubs_unresolved.c replaces the guest code at its address with a bare
`ret`. That is only right if the address is a function nobody found. If the address sits a few bytes
after the end of a lifted function and that function jumps (not calls) to it, it is that function's own
cold block/epilogue: the stub then skips its stores and its pops (lost callee-saved registers, esp off).
Out: out_stub_audit.txt + summary. Usage: py audit_stubs.py"""
import json, re, glob, bisect
GEN = r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\src\recomp\gen'
stubs = sorted(int(m, 16) for m in re.findall(r'void sub_([0-9A-F]{8})\(void\) \{ RECOMP_STUB_HIT',
                                                open(GEN + r'\recomp_stubs_unresolved.c').read()))
# function ranges and tail-jump sites from the generated headers/comments
rng = {}; jumps = {}
for f in glob.glob(GEN + r'\recomp_*.c'):
    cur = None
    for line in open(f, errors='replace'):
        m = re.match(r' \* Original: 0x([0-9A-F]{8}) - 0x([0-9A-F]{8})', line)
        if m: cur = int(m.group(1), 16); rng[cur] = int(m.group(2), 16); continue
        for t in re.findall(r'sub_([0-9A-F]{8})\(\); return; \} /\* j|sub_([0-9A-F]{8})\(\); return; /\* tail jmp', line):
            t = int(t[0] or t[1], 16)
            if t in stubs and cur is not None: jumps.setdefault(t, set()).add(cur)
starts = sorted(rng)
tail, other = [], []
for s in stubs:
    i = bisect.bisect_right(starts, s) - 1
    prev = starts[i] if i >= 0 else None
    near = prev is not None and 0 <= s - rng[prev] <= 0x40          # within 64 bytes after the previous function's end
    (tail if near and prev in jumps.get(s, ()) else other).append((s, prev, sorted(jumps.get(s, ()))))
with open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\out_stub_audit.txt', 'w') as o:
    o.write(f'{len(stubs)} unresolved stubs; {len(tail)} are tail blocks of the preceding function (jumped to from it, <=0x40 past its end)\n\n')
    for s, p, j in tail: o.write(f'TAIL  {s:08X}  of {p:08X} (ends {rng[p]:08X})  jumped from {" ".join(f"{x:08X}" for x in j)}\n')
    for s, p, j in other: o.write(f'OTHER {s:08X}  jumped from {" ".join(f"{x:08X}" for x in j) or "-"}\n')
print(f'{len(stubs)} stubs, {len(tail)} tail blocks, {len(other)} other -> out_stub_audit.txt')
