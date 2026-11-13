"""patch_d3d11_horplus.py - idempotent. Opt-in Hor+ widescreen for the D3D11 backend (S4.2). RECOMP_HORPLUS=16:9.

How (GO kit reports/research/S4.1-horplus-findings.md, updated 2026-11-12):
The game's vertex programs end with the XDK epilogue, so oPos leaves the program in guest surface pixels. The NV2A
clips in the rasterizer, not in the program, so a vertex outside the 4:3 view still has a valid (off-screen) position.
The generated HLSL maps pixels to NDC with cbuffer b2 (x * 2/w - 1). Dividing that x mapping by A (= target aspect /
4:3) squeezes the frame around its centre, which brings the off-screen sides into the frame: a wider horizontal view at
the same vertical view. Presenting that frame stretched to the target aspect gives the right proportions.
- Every draw into a 4:3 surface at least 640 wide (the main frame) is squeezed: 3D gets the wider view, and 2D
  (HUD, menus; with or without a program) stays a centred 4:3 picture. Render-to-texture is untouched. A draw that
  samples a render target with depth test off and at most 8 indices is a full-screen pass over the frame (seen
  running: squeezing it shrank a copy of the whole frame into the middle), so its unsqueezed mapping is put back once
  setup_pixel knows its textures. Shadow decals also sample render targets but test depth; they stay squeezed (seen
  running: unsqueezed, a shadow sat ~100 px beside its object).
- 2D (depth test off) squeezed draws are scissored to the 4:3 box: menus that ran off the 4:3 screen edge stay cut
  there (seen running: a main-menu panel overhung on the left). Full-screen passes get their full scissor back.
- Present: with Hor+ on, the frame is fitted as if it had the target aspect (filled, not letterboxed).
Off by default: unset or "4:3" leaves every draw and the present exactly as before.
Anchors must each occur once; refuses otherwise."""
import io
import os
import pathlib

P = pathlib.Path(os.environ.get("BREAKDOWN_D3D11_FILE")
                 or pathlib.Path(__file__).resolve().parent / "external" / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c")
MARK = "[d3d11-horplus]"

STATE = r'''/* {mark} Hor+: s_hp = 4:3 / target aspect (1 = off). RECOMP_HORPLUS=W:H, read once. */
static float s_hp = 1.0f;
static int s_hp_read;
static int s_hp_sq;               /* this draw was squeezed */
static uint8_t s_hp_vp[80];       /* its unsqueezed b2 bytes */
static unsigned long s_hp_post;   /* squeezed draws put back (they sampled a render target) */
static D3D11_RECT s_hp_full;      /* this draw's scissor before the 4:3 cut */
static float horplus(void)
{{
    if (!s_hp_read) {{
        const char *e = getenv("RECOMP_HORPLUS");
        double w = 0, h = 0;
        s_hp_read = 1;
        if (e && sscanf(e, "%lf:%lf", &w, &h) == 2 && w > 0 && h > 0 && w * 3.0 > h * 4.0 + 1e-6) {{
            s_hp = (float)((4.0 / 3.0) / (w / h));
            fprintf(stderr, "[d3d11] hor+ on: %s (3D x squeezed by %.4f, frame shown at %s)\n", e, s_hp, e);
        }} else if (e && *e) {{
            fprintf(stderr, "[d3d11] hor+ off: RECOMP_HORPLUS=%s is not wider than 4:3 (use W:H, e.g. 16:9)\n", e);
        }}
    }}
    return s_hp;
}}

'''


def main():
    s = io.open(P, encoding="utf-8", newline="").read()
    if MARK in s:
        print("already patched")
        return
    nl = "\r\n" if "\r\n" in s else "\n"

    def n(t):
        return t.replace("\n", nl)

    edits = [
        ("static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)",
         n(STATE.format(mark=MARK)) + "static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)"),
        ("        nv2a_vp_consts_fill(v, g->format, rt->w, rt->h, &vc);" + nl,
         "        nv2a_vp_consts_fill(v, g->format, rt->w, rt->h, &vc);" + nl
         + "        s_hp_sq = 0;" + nl
         + "        if (horplus() != 1.0f && rt->w >= 640 && rt->w * 3 == rt->h * 4) {"
         + " /* " + MARK + " any draw into the main frame */" + nl
         + "            memcpy(s_hp_vp, vc.c, sizeof vc.c);" + nl
         + "            memcpy(s_hp_vp + sizeof vc.c, vc.u, sizeof vc.u);" + nl
         + "            vc.c[0] *= s_hp;" + nl
         + "            vc.c[4] *= s_hp;" + nl
         + "            s_hp_sq = 1;" + nl
         + "        }" + nl),
        ("        sr.bottom = (LONG)hb[3];" + nl,
         "        sr.bottom = (LONG)hb[3];" + nl
         + "        s_hp_full = sr;" + nl
         + "        if (s_hp_sq && !v->depth_enable) { /* " + MARK + " 2D: cut at the 4:3 box, as the screen edge did */" + nl
         + "            LONG hl = (LONG)((float)rt->hw * (1.0f - s_hp) * 0.5f + 0.5f), hr = (LONG)rt->hw - hl;" + nl
         + "            if (sr.left < hl) sr.left = hl;" + nl
         + "            if (sr.right > hr) sr.right = hr;" + nl
         + "            if (sr.right < sr.left) sr.right = sr.left;" + nl
         + "        }" + nl),
        ("        if (memo_on() && vp_ok && !memcmp(vp_shadow, vp, sizeof vp)) {",
         "        if (memo_on() && vp_ok && !memcmp(vp_shadow, vp, sizeof vp)"
         + " && horplus() == 1.0f) { /* " + MARK + " no memo: may be re-uploaded */"),
        ("            ps = setup_pixel();" + nl,
         "            ps = setup_pixel();" + nl
         + "            if (s_hp_sq && !v->depth_enable && d->idx_count <= 8" + nl
         + "                    && (s_stage_rt[0] || s_stage_rt[1] || s_stage_rt[2] || s_stage_rt[3])) {" + nl
         + "                /* " + MARK + " a quad without depth test that samples a render target: a full-screen" + nl
         + "                 * pass over the frame, not the 3D scene (shadow decals sample render targets too, but" + nl
         + "                 * test depth). Put the unsqueezed mapping back. */" + nl
         + "                D3D11_MAPPED_SUBRESOURCE hm;" + nl
         + "                if (SUCCEEDED(ID3D11DeviceContext_Map(s_ctx, (ID3D11Resource *)s_cb_vp, 0," + nl
         + "                                                      D3D11_MAP_WRITE_DISCARD, 0, &hm))) {" + nl
         + "                    memcpy(hm.pData, s_hp_vp, sizeof s_hp_vp);" + nl
         + "                    ID3D11DeviceContext_Unmap(s_ctx, (ID3D11Resource *)s_cb_vp, 0);" + nl
         + "                }" + nl
         + "                ID3D11DeviceContext_RSSetScissorRects(s_ctx, 1, &s_hp_full);" + nl
         + "                s_sc.sr = s_hp_full;" + nl
         + "                s_hp_post++;" + nl
         + "            }" + nl),
        ("    whole = nv2a_present_rect(hcw, hch, s_back_w, s_back_h, filter, &r);",
         "    if (horplus() != 1.0f) { /* " + MARK + " fit as the target aspect, not 4:3 */" + nl
         + "        static unsigned hp_n;" + nl
         + "        hcw = (UINT)((float)hcw / s_hp + 0.5f);" + nl
         + "        if (++hp_n % 1800u == 0)" + nl
         + "            fprintf(stderr, LOGP \"hor+: %lu full-screen passes kept unsqueezed so far\\n\", s_hp_post);" + nl
         + "    }" + nl
         + "    whole = nv2a_present_rect(hcw, hch, s_back_w, s_back_h, filter, &r);"),
        ("        int blit = !stock && !(same && eo->present_filter != NV2A_PRESENT_LINEAR);",
         "        int blit = (!stock && !(same && eo->present_filter != NV2A_PRESENT_LINEAR))" + nl
         + "                   || horplus() != 1.0f; /* " + MARK + " always stretch */"),
    ]
    for old, new in edits:
        if s.count(old) != 1:
            raise SystemExit("REFUSED: anchor not found exactly once: %r" % old.strip()[:70])
    for old, new in edits:
        s = s.replace(old, new, 1)
    io.open(P, "w", encoding="utf-8", newline="").write(s)
    print("patched d3d11 hor+ (RECOMP_HORPLUS)")


if __name__ == "__main__":
    main()
