"""perf_summary.py [log] - average the per-flip [PB-PERF] timings: where does a frame's time go?
wall = time between flips; walk = executing the pushbuffer (draw = rasterising, decode = parsing methods);
kickwait = guest CPU blocked waiting for the executor; present = showing the frame."""
import re, sys, statistics as st
L = open(sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\bench_ac_err.txt', errors='replace').read().splitlines()
keys = ['wall', 'walk', 'draw', 'decode', 'present', 'kickwait']; rows = []
for l in L:
    if '[PB-PERF]' not in l: continue
    r = {k: float(m.group(1)) for k in keys if (m := re.search(rf'\b{k}\s+([\d.]+)', l))}
    m = re.search(r'kicks (\d+)', l); r['kicks'] = int(m.group(1)) if m else 0
    m = re.search(r'methods (\d+)', l); r['methods'] = int(m.group(1)) if m else 0
    rows.append(r)
print(f'{len(rows)} flips with [PB-PERF]')
for k in keys + ['kicks', 'methods']:
    v = [r[k] for r in rows if k in r]
    if v: print(f'  {k:9} median {st.median(v):8.2f}  mean {st.mean(v):8.2f}  max {max(v):8.2f}')
rast = [l for l in L if 'raster' in l and 'ms' in l and '[GPU] flip' in l][-3:]
for l in rast: print('  ', l.strip()[:200])
thr = [l.strip() for l in L if 'raster threads' in l.lower() or 'RASTER_THREADS' in l][:2]
for l in thr: print('  ', l[:160])
