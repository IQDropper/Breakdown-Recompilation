"""patch_skip_why2.py - idempotent. Log prim + vertex count of the first 60 draws that convert_indices rejects."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if 'skip-conv' in s:
    print('already patched'); raise SystemExit
old = 's_draws_skipped++; s_skip_why[4]++;'
assert s.count(old) == 1
new = (old + ' { static int nlog; if (nlog < 60) { nlog++; fprintf(stderr, LOGP "skip-conv #%d: prim %u idx_count %u'
       ' inline %d idx0 %u idx1 %u (present %llu)\\n", nlog, (unsigned)d->prim, (unsigned)d->idx_count, d->inline_data ? 1 : 0,'
       ' d->idx_count > 0 ? (unsigned)d->idx[0] : 0u, d->idx_count > 1 ? (unsigned)d->idx[1] : 0u,'
       ' (unsigned long long)s_presents); } }')
s = s.replace(old, new, 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
