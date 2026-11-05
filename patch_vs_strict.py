"""patch_vs_strict.py - idempotent. D3D11: compile vertex shaders with D3DCOMPILE_IEEE_STRICTNESS.
Why: Breakdown draws rooms as a depth pre-pass (LEQUAL, write) then colour passes with depth EQUAL.
EQUAL only passes when every pass computes bit-identical depth. Each pass has its own generated shader,
and the HLSL/driver compilers may fuse or reorder the position math differently per shader, so EQUAL
failed everywhere and the room was black (the CPU rasteriser runs one code path, so it was fine).
xemu solves the same thing with `invariant gl_Position`; strict IEEE is the D3D11 equivalent for a whole shader."""
import io
P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[vs-strict]' in s:
    print('already patched'); raise SystemExit
old = ('    HRESULT hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,\n'
       '                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code, &err);')
nl = '\r\n' if '\r\n' in s else '\n'
old = old.replace('\n', nl)
assert s.count(old) == 1, 'compile site'
new = ('    /* [vs-strict] vertex shaders bit-reproducible across passes (depth EQUAL after a pre-pass) */' + nl +
       '    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3' + nl +
       "               | (target[0] == 'v' && target[1] == 's' ? D3DCOMPILE_IEEE_STRICTNESS : 0);" + nl +
       '    HRESULT hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,' + nl +
       '                           flags, 0, code, &err);')
s = s.replace(old, new, 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
