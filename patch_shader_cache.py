"""patch_shader_cache.py - idempotent. D3D11 backend: keep every compiled shader on disk, keyed by its source, so a
shader is compiled once per PC instead of once per run.

Breakdown 2026-11-14 (P6.3): the worst frames after a section load were shader compiles (7F section A, frame 1574:
vertex shaders 148 ms + pixel shaders 205 ms of a 412 ms frame). With the cache, a shader seen before loads in well
under a millisecond. Folder: RECOMP_SHADER_CACHE, else <RECOMP_DATA_DIR or %LOCALAPPDATA%\\xboxrecomp>\\ShaderCache.
RECOMP_SHADER_CACHE=off turns it off. A file that cannot be read or used is compiled again and rewritten.
Applied by APPLY_RENDER_PATCHES.cmd."""
import io

P = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[shader-cache]' in s:
    print('already patched')
    raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'


def rep(old, new):
    global s
    old = old.replace('\n', nl)
    new = new.replace('\n', nl)
    assert s.count(old) == 1, 'site not found: ' + old[:60]
    s = s.replace(old, new, 1)


rep('''static int compile(const char *what, const char *src, const char *target,
                   ID3DBlob **code)
{''', '''/* [shader-cache] compiled shaders on disk, keyed by FNV-1a 64 of version + target + flags + source. */
typedef HRESULT (WINAPI *create_blob_fn)(SIZE_T, ID3DBlob **);
static create_blob_fn s_create_blob;
static char s_sc_dir[MAX_PATH];
static int s_sc_state;                 /* 0 not set up, 1 on, -1 off */
static unsigned s_sc_hits, s_sc_misses, s_sc_writes;

static int sc_setup(void)
{
    const char *e = getenv("RECOMP_SHADER_CACHE"), *base;
    if (s_sc_state)
        return s_sc_state > 0;
    s_sc_state = -1;
    if (e && !_stricmp(e, "off"))
        return 0;
    if (!s_create_blob) {
        HMODULE dll = GetModuleHandleA("d3dcompiler_47.dll");
        if (dll)
            s_create_blob = (create_blob_fn)(void (*)(void))GetProcAddress(dll, "D3DCreateBlob");
        if (!s_create_blob)
            return 0;
    }
    if (e && *e) {
        snprintf(s_sc_dir, sizeof s_sc_dir, "%s", e);
    } else {
        base = getenv("RECOMP_DATA_DIR");
        if (base && *base) {
            snprintf(s_sc_dir, sizeof s_sc_dir, "%s\\\\ShaderCache", base);
        } else {
            base = getenv("LOCALAPPDATA");
            if (!base || !*base)
                return 0;
            snprintf(s_sc_dir, sizeof s_sc_dir, "%s\\\\xboxrecomp", base);
            CreateDirectoryA(s_sc_dir, NULL);
            snprintf(s_sc_dir, sizeof s_sc_dir, "%s\\\\xboxrecomp\\\\ShaderCache", base);
        }
    }
    CreateDirectoryA(s_sc_dir, NULL);
    s_sc_state = 1;
    fprintf(stderr, "[shader-cache] %s\\n", s_sc_dir);
    return 1;
}

static void sc_path(char *out, size_t n, const char *src, const char *target, UINT flags)
{
    unsigned long long h = 1469598103934665603ULL;
    const char *parts[3] = { "v1", target, src };
    int i;
    for (i = 0; i < 3; i++) {
        const unsigned char *p = (const unsigned char *)parts[i];
        for (; *p; p++) { h ^= *p; h *= 1099511628211ULL; }
        h ^= 0xFF; h *= 1099511628211ULL;
    }
    h ^= flags; h *= 1099511628211ULL;
    snprintf(out, n, "%s\\\\%016llx.cso", s_sc_dir, h);
}

static int sc_load(const char *path, ID3DBlob **code)
{
    FILE *f = fopen(path, "rb");
    long len;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (16L << 20) || FAILED(s_create_blob((SIZE_T)len, code))) {
        fclose(f);
        return 0;
    }
    if (fread(ID3D10Blob_GetBufferPointer(*code), 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        ID3D10Blob_Release(*code);
        *code = NULL;
        return 0;
    }
    fclose(f);
    return 1;
}

static void sc_store(const char *path, ID3DBlob *code)
{
    char tmp[MAX_PATH + 8];
    FILE *f;
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f)
        return;
    if (fwrite(ID3D10Blob_GetBufferPointer(code), 1, ID3D10Blob_GetBufferSize(code), f)
            == ID3D10Blob_GetBufferSize(code)) {
        fclose(f);
        MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING);
        s_sc_writes++;
    } else {
        fclose(f);
        DeleteFileA(tmp);
    }
}

static int compile(const char *what, const char *src, const char *target,
                   ID3DBlob **code)
{
    char sc_file[MAX_PATH];''')

# A toolkit without the local [vs-strict] edit passes the level straight to s_compile: give it a flags variable
# first, so the cache key and the compile use the same flags either way.
_plain = '''    HRESULT hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,
                           D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, code, &err);'''.replace('\n', nl)
if s.count(_plain) == 1:
    s = s.replace(_plain, '''    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,
                           flags, 0, code, &err);'''.replace('\n', nl))

rep('''    HRESULT hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,
                           flags, 0, code, &err);''', '''    HRESULT hr;
    sc_file[0] = 0;
    if (sc_setup()) {   /* [shader-cache] */
        sc_path(sc_file, sizeof sc_file, src, target, flags);
        if (sc_load(sc_file, code)) {
            if ((++s_sc_hits & 63) == 1)
                fprintf(stderr, "[shader-cache] %u loaded from disk, %u compiled\\n", s_sc_hits, s_sc_misses);
            return 1;
        }
        s_sc_misses++;
    }
    hr = s_compile(src, strlen(src), what, NULL, NULL, "main", target,
                   flags, 0, code, &err);''')

rep('''    if (err) ID3D10Blob_Release(err);
    return 1;
}''', '''    if (err) ID3D10Blob_Release(err);
    if (sc_file[0])
        sc_store(sc_file, *code);   /* [shader-cache] */
    return 1;
}''')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched: [shader-cache]')
