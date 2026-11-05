"""find_str.py <file> <needle> [<needle>...]  - find ASCII and UTF-16LE strings (case-insensitive), print offset + context."""
import sys, re
data = open(sys.argv[1], 'rb').read()
low = data.lower()
for n in sys.argv[2:]:
    for enc in ('ascii', 'utf-16-le'):
        pat = n.lower().encode(enc)
        hits = [m.start() for m in re.finditer(re.escape(pat), low)][:8]
        for h in hits:
            s = data[max(0, h-40):h+60]
            txt = s.decode(enc if enc == 'ascii' else 'utf-16-le', 'replace') if enc == 'ascii' else data[h - (h % 2) - 40: h + 80].decode('utf-16-le', 'replace')
            txt = re.sub(r'[^\x20-\x7e]', '.', txt)
            print(f"{n!r:14} {enc:9} off=0x{h:07X}  {txt}")
