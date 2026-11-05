"""dis.py START END [START END ...] - disassemble guest VA ranges straight from default.xbe (capstone)."""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
x = open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\game_files\default.xbe', 'rb').read(); base, = struct.unpack_from('<I', x, 0x104)
n, sa = struct.unpack_from('<II', x, 0x11C); md = Cs(CS_ARCH_X86, CS_MODE_32)
def raw(va, size):
    for i in range(n):
        f, v, vs, r, rs = struct.unpack_from('<5I', x, sa - base + i * 56)
        if v <= va < v + rs: return x[r + va - v: r + va - v + size]
a = sys.argv[1:]
for s, e in zip(a[::2], a[1::2]):
    s, e = int(s, 16), int(e, 16); print(f'--- {s:#x}..{e:#x}')
    for ins in md.disasm(raw(s, e - s), s): print(f'  {ins.address:06x}  {ins.mnemonic:6} {ins.op_str}')
