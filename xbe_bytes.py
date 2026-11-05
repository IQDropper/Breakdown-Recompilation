"""xbe_bytes.py VA [n] - hex of n bytes at a guest VA, read from default.xbe via its section table.
Used to check what an 'unresolved stub' (recomp_stubs_unresolved.c) skipped."""
import struct, sys
xbe = open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\game_files\default.xbe', 'rb').read()
base = struct.unpack_from('<I', xbe, 0x104)[0]
nsec, secaddr = struct.unpack_from('<II', xbe, 0x11C)
va = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 32
for i in range(nsec):
    h = secaddr - base + i * 56
    flags, vaddr, vsize, raddr, rsize = struct.unpack_from('<5I', xbe, h)
    if vaddr <= va < vaddr + rsize:
        off = raddr + va - vaddr
        b = xbe[off:off + n]
        print(f'{va:#x} (file {off:#x}):', ' '.join(f'{x:02x}' for x in b))
        break
