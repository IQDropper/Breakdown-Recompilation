"""patch_tex_dump.py - idempotent. D3D11 backend: RECOMP_TEX_DUMP=<folder> writes each unique guest texture once.

Hooks tex_upload(): when the guest's texels are decoded into s_texels (A8R8G8B8, level 0 first), the
texture's level 0 is written as an uncompressed 32-bit TGA, named
    <hash16>_<w>x<h>_<fmt>.tga      (fmt = the NV2A colour format, 2 hex digits)
<hash16> is the SAME hash the toolkit's textures.pack lookup uses (nv2a_tex_hash over the texture bytes, with
the P8 palette folded in; see tex_hash_pal), so a replacement pack keyed by (hash, w, h) lines up with the
dump names. Uniqueness inside a run is by (hash, w, h, fmt), so the same bytes under another format or size
get their own file. Unset = one cached getenv, no other change. Capped at 40000 files per run (dynamic
textures that rewrite themselves every flip would otherwise fill the disk).

Refuses (exit 1) if an anchor is missing or occurs more than once. Re-running is a no-op."""
import io
import pathlib
import sys

P = pathlib.Path("external") / "xboxrecomp" / "src" / "d3d" / "nv2a_pb_d3d11.c"
MARK = "[tex-dump]"

if not P.exists():
    print("[skip] toolkit not present: %s" % P)
    sys.exit(0)

s = io.open(P, encoding="utf-8", newline="").read()
if MARK in s:
    print("already patched")
    sys.exit(0)
nl = "\r\n" if "\r\n" in s else "\n"


def need_once(text, what):
    n = s.count(text)
    if n != 1:
        print("[error] anchor '%s' found %d times (want 1); toolkit moved, patch not applied" % (what, n))
        sys.exit(1)


A1 = "/* `hash` is the entry's texel hash: with a texture pack loaded"
need_once(A1, "tex_upload comment")
A2 = "        src = s_texels;" + nl + "    }" + nl + "    /* BC3 levels go up as they are"
need_once(A2, "tex_upload decode tail")

helper = r'''/* [tex-dump] RECOMP_TEX_DUMP=<folder>: each unique guest texture (level 0, decoded A8R8G8B8) once, as an
 * uncompressed 32-bit BGRA TGA named <hash16>_<w>x<h>_<fmt>.tga. hash16 is the textures.pack hash
 * (tex_hash_pal), so replacements line up. Unique by (hash, w, h, fmt). Capped per run. */
#define TEXDUMP_CAP   40000u
#define TEXDUMP_SLOTS (1u << 17)
typedef struct { uint64_t hash; uint32_t wh; uint32_t fmt_used; } TexDumpKey;   /* fmt_used: bit 8 = slot used */
static TexDumpKey *s_texdump_set;
static const char *s_texdump_dir;
static int         s_texdump_init;
static uint32_t    s_texdump_n;
/* Dynamic textures (the intro movie, anything the game rewrites every flip) re-upload at one guest address
 * with a new hash each time. Per (offset, w, h, fmt) at most TEXDUMP_PER_LOC distinct hashes are dumped. */
#define TEXDUMP_PER_LOC 6u
#define TEXDUMP_LOCS    8192u
typedef struct { uint32_t offset, wh, fmt_used, n; } TexDumpLoc;
static TexDumpLoc *s_texdump_loc;
static uint32_t    s_texdump_skipped;

static void tex_dump(const Stage *t, uint64_t hash)
{
    char path[MAX_PATH + 64];
    uint8_t hd[18];
    uint32_t wh, fu, i, w = t->width, h = t->height;
    FILE *f;
    if (!s_texdump_init) {
        const char *d = getenv("RECOMP_TEX_DUMP");
        s_texdump_init = 1;
        if (d && *d) {
            s_texdump_set = (TexDumpKey *)calloc(TEXDUMP_SLOTS, sizeof(TexDumpKey));
            s_texdump_loc = (TexDumpLoc *)calloc(TEXDUMP_LOCS, sizeof(TexDumpLoc));
            if (s_texdump_set && s_texdump_loc) {
                CreateDirectoryA(d, NULL);
                s_texdump_dir = d;
                fprintf(stderr, LOGP "tex dump -> %s (cap %u)\n", d, TEXDUMP_CAP);
            }
        }
    }
    if (!s_texdump_dir || s_texdump_n >= TEXDUMP_CAP || !s_texels || !w || !h || w > 65535u || h > 65535u)
        return;
    wh = (w << 16) | h;
    fu = 0x100u | (t->color & 0xFFu);
    /* exact key: hash, w, h and fmt all stored and compared; the probe start is a mix of them */
    i = (uint32_t)((hash ^ (hash >> 29)) * 0x9E3779B1u + wh * 0x85EBCA6Bu + fu) & (TEXDUMP_SLOTS - 1u);
    for (;; i = (i + 1u) & (TEXDUMP_SLOTS - 1u)) {
        TexDumpKey *k = &s_texdump_set[i];
        if (!k->fmt_used) {
            k->hash = hash;
            k->wh = wh;
            k->fmt_used = fu;
            break;
        }
        if (k->hash == hash && k->wh == wh && k->fmt_used == fu)
            return;
    }
    {   /* per-location budget; the key above is already recorded, so a skipped hash is not retried */
        uint32_t j = (t->offset * 2654435761u ^ wh * 40503u ^ fu) & (TEXDUMP_LOCS - 1u), tries;
        TexDumpLoc *lc = NULL;
        for (tries = 0; tries < TEXDUMP_LOCS; tries++, j = (j + 1u) & (TEXDUMP_LOCS - 1u)) {
            TexDumpLoc *c = &s_texdump_loc[j];
            if (!c->fmt_used || (c->offset == t->offset && c->wh == wh && c->fmt_used == fu)) {
                lc = c;
                break;
            }
        }
        if (lc) {
            lc->offset = t->offset; lc->wh = wh; lc->fmt_used = fu;
            if (lc->n >= TEXDUMP_PER_LOC) {
                if (!s_texdump_skipped++)
                    fprintf(stderr, LOGP "tex dump: dynamic texture at 0x%08X %ux%u skipped (rewritten every flip)\n",
                            t->offset, w, h);
                return;
            }
            lc->n++;
        }
    }
    snprintf(path, sizeof path, "%s\\%016llx_%ux%u_%02X.tga", s_texdump_dir,
             (unsigned long long)hash, w, h, t->color & 0xFFu);
    f = fopen(path, "wb");
    if (!f)
        return;
    memset(hd, 0, sizeof hd);
    hd[2] = 2;                       /* uncompressed true-colour */
    hd[12] = (uint8_t)w; hd[13] = (uint8_t)(w >> 8);
    hd[14] = (uint8_t)h; hd[15] = (uint8_t)(h >> 8);
    hd[16] = 32;
    hd[17] = 0x28;                   /* 8 alpha bits, top-left origin */
    fwrite(hd, 1, sizeof hd, f);
    fwrite(s_texels, 4, (size_t)w * h, f);   /* A8R8G8B8 little-endian = BGRA bytes, as TGA wants */
    fclose(f);
    s_texdump_n++;
    if (s_texdump_n == 1 || (s_texdump_n % 500u) == 0 || s_texdump_n == TEXDUMP_CAP)
        fprintf(stderr, LOGP "tex dump: %u files (%u dynamic hashes skipped)%s\n", s_texdump_n,
                s_texdump_skipped, s_texdump_n == TEXDUMP_CAP ? " (cap reached)" : "");
}

'''.replace("\n", nl)

s = s.replace(A1, helper + A1, 1)
s = s.replace(A2, "        src = s_texels;" + nl + "        tex_dump(t, hash);   /* [tex-dump] */" + nl + "    }" + nl
              + "    /* BC3 levels go up as they are", 1)
io.open(P, "w", encoding="utf-8", newline="").write(s)
print("patched: RECOMP_TEX_DUMP texture dump")
