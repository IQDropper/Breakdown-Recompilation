"""patch_skip_why.py - idempotent. D3D11 backend: count WHY draws are skipped (one counter per reason)
and print the breakdown with the every-300-presents summary. Why: 'N skipped' alone can't tell
'no render target' from 'vertex shader failed' from 'clip box empty' - each needs a different fix."""
import io, re
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if 's_skip_why' in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('static uint64_t s_presents, s_clears, s_draws, s_draws_skipped;',
              'static uint64_t s_presents, s_clears, s_draws, s_draws_skipped;' + nl +
              '/* [skip-why] 0 no rt, 1 no vertex shader, 2 no indices, 3 idx grow, 4 convert_indices 0,'
              ' 5 vb alloc, 6 vb alloc f32, 7 ib map, 8 empty draw box */' + nl +
              'static uint64_t s_skip_why[9];', 1)
old = 'if (!rt || !vs || !d->idx_count) {' + nl + '        s_draws_skipped++;'
assert s.count(old) == 1, 'first site'
s = s.replace(old, 'if (!rt || !vs || !d->idx_count) {' + nl +
              '        s_draws_skipped++; s_skip_why[!rt ? 0 : !vs ? 1 : 2]++;', 1)
# remaining sites in order: 3..8
k = 3
def rep(m):
    global k
    r = m.group(0) + f' s_skip_why[{k}]++;'; k += 1; return r
start = s.index('s_skip_why[!rt')
head, tail = s[:start + 40], s[start + 40:]
tail = re.sub(r's_draws_skipped\+\+;', rep, tail, count=6)
assert k == 9, k
s = head + tail
old = '    if (s_presents == 1 || (s_presents % 300) == 0)' + nl + '        fprintf(stderr, LOGP "present #'
assert s.count(old) == 1, 'summary site'
s = s.replace(old, '    if ((s_presents % 300) == 0 && s_draws_skipped)' + nl +
    '        fprintf(stderr, LOGP "skip-why: rt %llu vs %llu noidx %llu grow %llu conv %llu vb %llu vbf %llu ib %llu box %llu\\n",' + nl +
    '                (unsigned long long)s_skip_why[0], (unsigned long long)s_skip_why[1], (unsigned long long)s_skip_why[2],' + nl +
    '                (unsigned long long)s_skip_why[3], (unsigned long long)s_skip_why[4], (unsigned long long)s_skip_why[5],' + nl +
    '                (unsigned long long)s_skip_why[6], (unsigned long long)s_skip_why[7], (unsigned long long)s_skip_why[8]);' + nl + old, 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
