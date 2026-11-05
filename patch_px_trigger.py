"""patch_px_trigger.py - idempotent. D3D11 pixel probe on demand: RECOMP_D3D11_PX_TRIGGER=<file>.
While the game runs, creating <file> arms the probe for the NEXT presented frame only; the game deletes
the file, so each `echo > file` captures one frame. Why: PX_FLIPS needs the frame number in advance,
and probing a wide range slows the game to a crawl (every draw is read back)."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[px-trigger]' in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'
old = '    return s_px_n > 0 && s_px_max > 0 && s_presents >= s_px_f0 && s_presents <= s_px_f1;'
assert s.count(old) == 1
new = ('    /* [px-trigger] */' + nl +
       '    {' + nl +
       '        static const char *trig = (const char *)1;' + nl +
       '        static uint64_t checked = ~0ull;' + nl +
       '        if (trig == (const char *)1) {' + nl +
       '            trig = getenv("RECOMP_D3D11_PX_TRIGGER");' + nl +
       '            if (trig && s_px_n) { s_px_f0 = s_px_f1 = ~0ull; }' + nl +
       '        }' + nl +
       '        if (trig && s_px_n && checked != s_presents && (s_presents % 8) == 0) {' + nl +
       '            FILE *tf;' + nl +
       '            checked = s_presents;' + nl +
       '            if ((tf = fopen(trig, "rb")) != NULL) {' + nl +
       '                fclose(tf);' + nl +
       '                remove(trig);' + nl +
       '                { const char *tn = getenv("RECOMP_D3D11_PX_TRIGGER_N"); s_px_f0 = s_presents + 1; s_px_f1 = s_px_f0 + (tn ? (uint64_t)atoi(tn) : 1u) - 1u; }' + nl +
       '                fprintf(stderr, LOGP "px: triggered, probing frame %llu\\n", (unsigned long long)s_px_f0);' + nl +
       '            }' + nl +
       '        }' + nl +
       '    }' + nl + old)
s = s.replace(old, new, 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
