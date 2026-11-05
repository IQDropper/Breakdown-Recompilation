"""sample_roles.py - group gdb stack samples by the toolkit's thread role ([THREAD] tid=N role=X in sample_err.txt)
and show each role's most common first NON-system frame (skips ntdll/KERNEL32/ucrtbase/driver frames)."""
import re, collections
err = open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\sample_err.txt', errors='replace').read()
roles = {int(t): r for t, r in re.findall(r'\[THREAD\] tid=(\d+) role=(\S+)', err)}
txt = open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\samples.txt', errors='replace').read()
SYS = re.compile(r'^(ntdll|KERNEL32|KERNELBASE|ucrtbase|igd\w+|nv\w+|d3d11|dxgi|XAudio2_\d+|\?\?|SleepConditionVariableSRW|win32u|USER32|msvcrt|combase|Dll\w+)$')
by = collections.defaultdict(collections.Counter)
for s in txt.split('=== SAMPLE')[1:]:
    for th in re.split(r'\n(?=Thread \d+ )', s):
        m = re.match(r'Thread \d+ \(Thread (\d+)\.0x([0-9a-f]+)\)', th)
        if not m: continue
        tid = int(m.group(2), 16)
        frames = re.findall(r'^#\d+\s+(?:0x[0-9a-f]+ in )?([\w:~<>.@?$]+)', th, re.M)
        if any(re.search(r'Wait|Sleep|NtDelay|GetMessage|SuspendThread', f) for f in frames[:5]): continue
        first = next((f for f in frames if not SYS.match(f)), '(system)')
        by[roles.get(tid, f'tid{tid}')][first] += 1
for role, c in sorted(by.items(), key=lambda kv: -sum(v for k, v in kv[1].items() if k != '(system/idle)')):
    busy = sum(v for k, v in c.items() if k != '(system/idle)')
    print(f'{role:24} busy {busy:3}/{sum(c.values()):3}  ' + ', '.join(f'{k} {v}' for k, v in c.most_common(4)))
