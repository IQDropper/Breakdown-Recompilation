"""patch_vsh_zeromul.py - idempotent. Vertex shaders: 0 * anything = 0, as on the Xbox GPU.

The NV2A vertex unit follows the old Direct3D rule that a product with a zero factor is zero, even when the other
factor is infinite or NaN. A PC GPU follows IEEE: 0 * inf = NaN, and a NaN colour draws black. Breakdown's light
maths computes an attenuation with RCP (1 / distance terms, infinite when a term is 0) and multiplies it by an
in-range factor that is 0 for surfaces outside the light. When the MP5's muzzle light switched on, every surface
outside its range got 0 * inf = NaN and went black (Foster 2026-11-27, 7F corridor: left wall and ceiling black
during the flash, right wall lit; not caused by Hor+ or 60 FPS). MUL, MAD, DP3, DPH and DP4 now use helpers that
return 0 for any zero factor. RECOMP_VSH_IEEE=1 brings back the plain IEEE products (A/B).
The generated HLSL changes, so the shader cache (keyed by source) compiles these vertex shaders once more."""
import io
import os
import pathlib

ROOT = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent)
P = ROOT / "external" / "xboxrecomp" / "src" / "d3d" / "d3d8_vsh_hlsl.c"
MARK = "[vsh-zeromul]"
s = io.open(P, encoding="utf-8", newline="").read()
if MARK in s:
    print("already patched")
    raise SystemExit
nl = "\r\n" if "\r\n" in s else "\n"


def sub(old, new, what):
    global s
    old = old.replace("\n", nl)
    new = new.replace("\n", nl)
    assert s.count(old) == 1, what
    s = s.replace(old, new, 1)


# 1. the MAC ops with a product
sub('''    case NV2A_VSH_MAC_MUL:
        emit_source(sb, A, 0); sb_append(sb, " * "); emit_source(sb, B, 0);
        break;
    case NV2A_VSH_MAC_ADD:
        emit_source(sb, A, 0); sb_append(sb, " + "); emit_source(sb, C, 0);
        break;
    case NV2A_VSH_MAC_MAD:
        emit_source(sb, A, 0); sb_append(sb, " * "); emit_source(sb, B, 0);
        sb_append(sb, " + "); emit_source(sb, C, 0);
        break;
    case NV2A_VSH_MAC_DP3:
        sb_append(sb, "dot(("); emit_source(sb, A, 0);
        sb_append(sb, ").xyz, ("); emit_source(sb, B, 0);
        sb_append(sb, ").xyz).xxxx");
        break;
    case NV2A_VSH_MAC_DPH:
        sb_append(sb, "(dot(("); emit_source(sb, A, 0);
        sb_append(sb, ").xyz, ("); emit_source(sb, B, 0);
        sb_append(sb, ").xyz) + ("); emit_source(sb, B, 0);
        sb_append(sb, ").w).xxxx");
        break;
    case NV2A_VSH_MAC_DP4:
        sb_append(sb, "dot("); emit_source(sb, A, 0);
        sb_append(sb, ", "); emit_source(sb, B, 0);
        sb_append(sb, ").xxxx");
        break;
''', '''    /* ''' + MARK + ''' products go through zm/zdp3/zdph/zdp4 (0 * anything = 0, as on the NV2A) */
    case NV2A_VSH_MAC_MUL:
        sb_append(sb, "zm("); emit_source(sb, A, 0); sb_append(sb, ", "); emit_source(sb, B, 0); sb_append(sb, ")");
        break;
    case NV2A_VSH_MAC_ADD:
        emit_source(sb, A, 0); sb_append(sb, " + "); emit_source(sb, C, 0);
        break;
    case NV2A_VSH_MAC_MAD:
        sb_append(sb, "zm("); emit_source(sb, A, 0); sb_append(sb, ", "); emit_source(sb, B, 0);
        sb_append(sb, ") + "); emit_source(sb, C, 0);
        break;
    case NV2A_VSH_MAC_DP3:
        sb_append(sb, "zdp3("); emit_source(sb, A, 0);
        sb_append(sb, ", "); emit_source(sb, B, 0);
        sb_append(sb, ").xxxx");
        break;
    case NV2A_VSH_MAC_DPH:
        sb_append(sb, "zdph("); emit_source(sb, A, 0);
        sb_append(sb, ", "); emit_source(sb, B, 0);
        sb_append(sb, ").xxxx");
        break;
    case NV2A_VSH_MAC_DP4:
        sb_append(sb, "zdp4("); emit_source(sb, A, 0);
        sb_append(sb, ", "); emit_source(sb, B, 0);
        sb_append(sb, ").xxxx");
        break;
''', "MAC ops")

# 2. the helpers, right after the constant buffer
sub('''        "cbuffer VSH_Constants : register(b1) {\\n"
        "    float4 c[%d];\\n"
        "};\\n"
        "\\n", NV2A_VS_MAX_CONSTANTS);
''', '''        "cbuffer VSH_Constants : register(b1) {\\n"
        "    float4 c[%d];\\n"
        "};\\n"
        "\\n", NV2A_VS_MAX_CONSTANTS);
    {   /* ''' + MARK + ''' NV2A products: a zero factor gives 0, never NaN (RECOMP_VSH_IEEE=1: plain IEEE) */
        const char *ieee = getenv("RECOMP_VSH_IEEE");
        if (ieee && ieee[0] == '1')
            sb_append(&sb,
                "float4 zm(float4 a, float4 b) { return a * b; }\\n"
                "float  zdp3(float4 a, float4 b) { return dot(a.xyz, b.xyz); }\\n"
                "float  zdph(float4 a, float4 b) { return dot(a.xyz, b.xyz) + b.w; }\\n"
                "float  zdp4(float4 a, float4 b) { return dot(a, b); }\\n\\n");
        else
            sb_append(&sb,
                "float4 zm(float4 a, float4 b) {\\n"
                "    float4 p = a * b;\\n"
                "    return (a != 0.0 && b != 0.0) ? p : (float4)0.0;\\n"
                "}\\n"
                "float  zdp3(float4 a, float4 b) { float4 p = zm(a, b); return p.x + p.y + p.z; }\\n"
                "float  zdph(float4 a, float4 b) { float4 p = zm(a, b); return p.x + p.y + p.z + b.w; }\\n"
                "float  zdp4(float4 a, float4 b) { float4 p = zm(a, b); return p.x + p.y + p.z + p.w; }\\n\\n");
    }
''', "prelude")

if "#include <stdlib.h>" not in s:
    first = s.index("#include")
    s = s[:first] + "#include <stdlib.h>   /* " + MARK + " getenv */" + nl + s[first:]
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("patched")
