"""load_report.py - a table from the [LOAD] lines a run writes with RECOMP_LOAD_LOG=1 (src/load_timer.c).

    python tools/load_report.py LOG [LOG ...] [--md out.md]

One row per area/section: loads seen, average and worst load time, and for the 5 s after the load the worst
frame and the stutter frames (over 50 ms). Loading plan step 1 (kit docs/LOADING_PLAN.md).
"""
import re
import sys
from collections import defaultdict

LOAD = re.compile(r'^\[LOAD\] (area|section) (\S+): (\d+) ms')
AFTER = re.compile(r'^\[LOAD\] after (\S+): worst frame ([\d.]+) ms, (\d+) frames over 50 ms')


def main():
    args = sys.argv[1:]
    out = None
    if '--md' in args:
        out = args[args.index('--md') + 1]
        args = args[:args.index('--md')] + args[args.index('--md') + 2:]
    loads = defaultdict(list)
    after = defaultdict(list)
    kind = {}
    for path in args:
        for ln in open(path, encoding='utf-8', errors='replace'):
            m = LOAD.match(ln)
            if m:
                kind[m.group(2)] = m.group(1)
                loads[m.group(2)].append(int(m.group(3)))
                continue
            m = AFTER.match(ln)
            if m:
                after[m.group(1)].append((float(m.group(2)), int(m.group(3))))
    rows = ['| Load | Kind | Seen | Average | Worst | Worst frame after | Stutter frames after (avg) |',
            '|---|---|---|---|---|---|---|']
    for name in sorted(loads, key=lambda n: (kind[n] != 'area', n)):
        v = loads[name]
        a = after.get(name, [])
        wf = '%.0f ms' % max(x[0] for x in a) if a else '-'
        sf = '%.1f' % (sum(x[1] for x in a) / len(a)) if a else '-'
        rows.append('| %s | %s | %d | %.0f ms | %d ms | %s | %s |' % (name, kind[name], len(v), sum(v) / len(v), max(v), wf, sf))
    text = '\n'.join(rows) + '\n'
    print(text)
    if out:
        open(out, 'w', encoding='utf-8').write('# Load times\n\nFrom ' + ', '.join(args) + '\n\n' + text)


if __name__ == '__main__':
    main()
