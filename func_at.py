"""func_at.py VA... - what analysis/disasm/functions.json says about functions at/around these VAs."""
import json, sys
fs = json.load(open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\analysis\disasm\functions.json'))
fs = fs.get('functions', fs) if isinstance(fs, dict) else fs
items = list(fs.values()) if isinstance(fs, dict) else fs
def start(f): v = f.get('start', f.get('address')); return int(v, 16) if isinstance(v, str) else v
items.sort(key=start)
for a in (int(x, 16) for x in sys.argv[1:]):
    for f in items:
        s = start(f); e = f.get('end'); e = int(e, 16) if isinstance(e, str) else e
        if s - 0x60 <= a <= (e or s) + 0x60:
            print(f'{a:#x}: func {s:#x}-{e:#x}', {k: v for k, v in f.items() if k not in ('start', 'end', 'calls_to', 'called_by', 'address')})
