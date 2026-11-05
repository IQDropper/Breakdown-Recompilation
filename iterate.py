"""iterate.py - automate the xboxrecomp porting loop overnight:
   run exe (N s) -> if '[ICALL] Failed to resolve VA 0x...' -> add observed seed -> analyze + recomp + build -> repeat.
   Stops on: a different failure kind (needs a human/AI look), no progress, or max iterations.
   First pass also seeds every function start Ghidra found in Phase 1 (pipeline\\out\\04_ghidra_fresh\\functions.csv).
LOG: iterate_log.txt (one line per iteration) ; run_stdout/err.txt of the last run."""
import csv, json, os, re, subprocess, sys, time
ROOT = r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown'; os.chdir(ROOT)
ENV = dict(os.environ, PATH=r'C:\Users\iron_\AppData\Roaming\Python\Python312\Scripts;' + os.environ['PATH'])
SEEDS = os.path.join(ROOT, 'config', 'seed_functions.json')
MAX = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 25
def log(msg):
    line = time.strftime('%H:%M:%S ') + msg; print(line, flush=True)
    open('iterate_log.txt', 'a').write(line + '\n')
def stage(cmd):
    r = subprocess.run(f'breakdown.cmd {cmd}', shell=True, env=ENV, capture_output=True, text=True, errors='replace')
    open(f'log_{cmd}.txt', 'w').write(r.stdout + r.stderr); return r.returncode
def run(secs=30):
    with open('run_stdout.txt', 'w') as o, open('run_stderr.txt', 'w') as e:
        p = subprocess.Popen([r'build-win\breakdown_recomp.exe'], cwd=ROOT, stdout=o, stderr=e)
        try: p.wait(secs); alive = False
        except subprocess.TimeoutExpired: p.kill(); alive = True
    err = open('run_stderr.txt', errors='replace').read()
    m = re.search(r'\[ICALL\] Failed to resolve VA (0x[0-9A-Fa-f]+)', err)
    kcalls = re.findall(r'summary: (\d+) total calls', err)
    return alive, (m.group(1).lower() if m else None), (int(kcalls[-1]) if kcalls else 0), err

seeds = json.load(open(SEEDS))
have = {s['start'].lower() for s in seeds}
if '--ghidra' in sys.argv and not any(s.get('source') == 'ghidra-phase1' for s in seeds):   # OFF by default: Ghidra's boundaries split real functions -> lost registers (0xCCCCCCCC crash)
    for r in csv.DictReader(open(r'D:\Emulation\RE\pipeline\out\04_ghidra_fresh\functions.csv')):
        a = '0x' + r['address'].lower().lstrip('0')
        if a not in have: seeds.append({'start': a, 'source': 'ghidra-phase1', 'observed': False}); have.add(a)
    json.dump(seeds, open(SEEDS, 'w'), indent=1)
    log(f'seeded {len(seeds)} Ghidra function starts; rebuilding')
    for c in ('analyze', 'recomp', 'build'):
        if stage(c): log(f'{c} FAILED (see log_{c}.txt)'); sys.exit(1)
last = None
if '--rebuild' in sys.argv:                  # seeds changed by hand -> regenerate before the first run
    for c in ('analyze', 'recomp', 'build'):
        if stage(c): log(f'{c} FAILED (see log_{c}.txt)'); sys.exit(1)
for it in range(1, MAX + 1):
    alive, va, kc, err = run()
    if alive: log(f'iter {it}: still running after 30 s ({kc} kernel calls) - stopping loop: needs eyes'); break
    if not va:
        tail = [l for l in err.splitlines() if l.strip() and 'ordinal' not in l][-6:]
        log(f'iter {it}: exited, no ICALL failure ({kc} kernel calls). tail: ' + ' | '.join(tail)); break
    if va == last: log(f'iter {it}: same ICALL {va} again after seeding - stopping'); break
    seeds.append({'start': va, 'source': f'icall iter {it}', 'observed': True}); json.dump(seeds, open(SEEDS, 'w'), indent=1)
    log(f'iter {it}: ICALL {va} ({kc} kernel calls) -> seeded, rebuilding'); last = va
    for c in ('analyze', 'recomp', 'build'):
        if stage(c): log(f'{c} FAILED after seeding {va} (see log_{c}.txt)'); sys.exit(1)
log('loop done')
