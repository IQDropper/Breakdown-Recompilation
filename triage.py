"""triage.py [log] - boil a native-run log (run_stderr.txt, often 10k+ lines) down to ~10-25 lines:
crash (+ named guest stack), unresolved / wild indirect calls (with call site), untranslated instructions,
missing files, input, frame counter. Deterministic - no LLM. Names come from the Ghidra export.
Use this BEFORE reading any log by hand (token saver)."""
import re, sys, csv, bisect, collections, os
LOG = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\run_stderr.txt'
FUNCS = r'D:\Emulation\RE\pipeline\out\07_named\functions.csv'
rows = sorted((int(r['address'], 16), r['name']) for r in csv.DictReader(open(FUNCS, encoding='utf-8', errors='replace')))
st = [a for a, _ in rows]
def name(va):
    i = bisect.bisect_right(st, va) - 1
    return f'{rows[i][1]}+{va - rows[i][0]:#x}' if i >= 0 else '?'
L = open(LOG, encoding='utf-8', errors='replace').read().splitlines()
out = [f'{os.path.basename(LOG)}: {len(L)} lines, modified {__import__("time").ctime(os.path.getmtime(LOG))}']
uniq = lambda xs: list(collections.OrderedDict.fromkeys(xs))
icall = uniq(re.sub(r'\(total calls: \d+\)', '', l).strip() for l in L if '[ICALL] Failed' in l)
for l in icall[:6]:
    m = re.search(r'VA (0x[0-9A-Fa-f]+)', l); out.append(f'UNRESOLVED CALL {m.group(1)} ({name(int(m.group(1), 16))})')
for l in uniq(l.strip() for l in L if 'is not code' in l)[:6]:
    m = re.search(r'target (0x\w+).*?site-ret=(0x\w+)', l)
    out.append(f'WILD CALL target {m.group(1)} from {name(int(m.group(2), 16))}' if m else 'WILD CALL ' + l[:120])
for l in uniq(re.sub(r'\s*\(a no-op.*', '', l.strip()) for l in L if '[UNIMPL]' in l)[:4]: out.append(l[:120])
miss = uniq(re.sub(r'\s*\(disp.*?\)', '', l.strip().split(' -> ')[0]) for l in L if '[FILE]' in l and 'FAILED' in l)
if miss: out.append(f'MISSING FILES ({len(miss)}): ' + ', '.join(m.replace('[FILE] ', '') for m in miss[:8]))
for l in uniq(l.strip() for l in L if l.startswith('[INPUT]'))[:3]: out.append(l)
peeks = [l.strip() for l in L if '[PEEK] 0x0029BB18' in l]
if peeks: out.append(f'FRAME COUNTER: {len(peeks)} samples, last {peeks[-1].split("=")[1].split()[0]}')
ci = next((i for i, l in enumerate(L) if '[CRASH]' in l), None)
if ci is not None:
    out.append(L[ci].strip())
    for l in L[ci + 1: ci + 4]:
        if 'regs' in l or 'VA of fault' in l: out.append('  ' + l.strip())
    rets = [int(m, 16) for l in L[ci:ci + 40] for m in re.findall(r'\]\s+(0x[0-9A-Fa-f]{8})\s*$', l)]
    out.append('  guest stack: ' + ' <- '.join(name(r) for r in rets[:7]))
else:
    out.append('no crash recorded (closed by user, hung, or still running)')
print('\n'.join(out))
