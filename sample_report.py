"""sample_report.py [samples.txt] - tally gdb stack samples: per thread role, which function is on top
(exclusive) and which appear anywhere in the stack (inclusive). Idle waits are listed separately."""
import re, sys, collections
txt = open(sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\samples.txt', errors='replace').read()
samples = txt.split('=== SAMPLE')[1:]
IDLE = re.compile(r'NtWaitFor|WaitForSingle|WaitForMultiple|ZwWait|NtDelayExecution|SleepEx|Sleep\b|NtRemoveIoCompletion|ZwRemoveIo|RtlSleepCondition|NtUserGetMessage|GetMessage|ZwUserMsgWait|MsgWaitFor|NtWaitForAlertByThreadId|SwitchToThread|NtYield')
top, incl, idle = collections.Counter(), collections.Counter(), 0
for s in samples:
    for th in re.split(r'\nThread \d+ ', s)[1:]:
        frames = re.findall(r'^#\d+\s+(?:0x[0-9a-f]+ in )?([\w:~<>.@?$]+)', th, re.M)
        if not frames: continue
        if any(IDLE.search(f) for f in frames[:3]): idle += 1; continue
        top[frames[0]] += 1
        for f in set(frames): incl[f] += 1
n = len(samples)
print(f'{n} samples; {idle} idle thread-samples skipped; busy thread-samples: {sum(top.values())}')
print('TOP (exclusive) - where a busy thread actually was:')
for f, c in top.most_common(18): print(f'  {c:4}  {f}')
print('INCLUSIVE (anywhere on a busy stack):')
for f, c in incl.most_common(22): print(f'  {c:4}  {f}')
