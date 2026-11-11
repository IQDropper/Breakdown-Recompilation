/*
 * display_host.c - RECOMP_RENDER_SCALE, RECOMP_WINDOW_SCALE,
 * RECOMP_DISPLAY_ASPECT, RECOMP_PRESENT_FILTER, RECOMP_PRESENT_FULLSCREEN.
 *
 * Read with getenv so they work without the enhancements layer. At toolkit
 * pin 0692070 that layer's display.aspect other than 4:3 is logged and
 * forced back to 4:3 (Hor+). Here aspect only sizes the output window;
 * present letterboxes the 4:3 frame. RECOMP_PRESENT_FULLSCREEN=exclusive
 * asks for DXGI exclusive on the desktop mode (patch_d3d11_exclusive.py).
 */
#include "display_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct aspect_ratio {
    const char *name;
    unsigned num, den; /* width / height; 0 num means use 640x480 exactly */
};

static const struct aspect_ratio s_aspects[] = {
    { "4:3",   0,  0 },
    { "16:9",  16, 9 },
    { "16:10", 16, 10 },
    { "21:9",  21, 9 },
};

static const char *s_filters[] = { "nearest", "linear", "integer" };

static const char *env_get(display_getenv_fn get, const char *name)
{
    const char *v = get ? get(name) : NULL;
    return (v && *v) ? v : NULL;
}

static void choose_aspect(const char *s, struct display_choice *o,
                          unsigned *num, unsigned *den)
{
    unsigned i;
    if (!s)
        s = "4:3";
    for (i = 0; i < sizeof s_aspects / sizeof s_aspects[0]; i++) {
        if (strcmp(s, s_aspects[i].name) == 0) {
            snprintf(o->aspect, sizeof o->aspect, "%s", s_aspects[i].name);
            *num = s_aspects[i].num;
            *den = s_aspects[i].den;
            return;
        }
    }
    o->aspect_unknown = 1;
    snprintf(o->aspect, sizeof o->aspect, "4:3");
    *num = 0;
    *den = 0;
}

/* 0..2, or -1 when s is missing or not a known word (caller treats as unset). */
static int choose_filter(const char *s, struct display_choice *o)
{
    int i;
    if (!s)
        return -1;
    for (i = 0; i < 3; i++) {
        if (strcmp(s, s_filters[i]) == 0) {
            snprintf(o->filter_name, sizeof o->filter_name, "%s", s_filters[i]);
            return i;
        }
    }
    return -1;
}

/* 0 windowed, 1 borderless, 2 exclusive. Unknown words are windowed, which
 * is what a non-empty value other than 1/true/yes used to be. */
static int choose_fullscreen(const char *s)
{
    if (!s)
        return DISPLAY_WINDOWED;
    if (strcmp(s, "exclusive") == 0 || strcmp(s, "2") == 0)
        return DISPLAY_EXCLUSIVE;
    if (strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "yes") == 0
        || strcmp(s, "borderless") == 0)
        return DISPLAY_BORDERLESS;
    return DISPLAY_WINDOWED;
}

static display_getenv_fn s_layered_env;
static display_getenv_fn s_layered_file;

void display_host_set_sources(display_getenv_fn env, display_getenv_fn file)
{
    s_layered_env = env;
    s_layered_file = file;
}

char *display_getenv_layered(const char *name)
{
    char *v = s_layered_env ? s_layered_env(name) : NULL;
    if (v && *v)
        return v;
    return s_layered_file ? s_layered_file(name) : NULL;
}

void display_host_choose(display_getenv_fn get, struct display_choice *o)
{
    const char *s;
    unsigned num = 0, den = 0;
    long scale, wscale;
    char *end;
    int filter_set = 0;

    memset(o, 0, sizeof *o);
    o->render_scale = 1;

    s = env_get(get, "RECOMP_RENDER_SCALE");
    if (s) {
        scale = strtol(s, &end, 10);
        if (end == s || *end || scale < 1 || scale > 4) {
            o->scale_clamped = 1;
            if (scale < 1)
                scale = 1;
            if (scale > 4)
                scale = 4;
        }
        o->render_scale = (unsigned)scale;
    }

    wscale = 1;
    s = env_get(get, "RECOMP_WINDOW_SCALE");
    if (s) {
        wscale = strtol(s, &end, 10);
        if (end == s || *end || wscale < 1 || wscale > 6) {
            o->window_clamped = 1;
            if (wscale < 1)
                wscale = 1;
            if (wscale > 6)
                wscale = 6;
        }
    }

    choose_aspect(env_get(get, "RECOMP_DISPLAY_ASPECT"), o, &num, &den);
    o->window_h = (unsigned)(480 * wscale);
    if (!num || !den)
        o->window_w = (unsigned)(640 * wscale);
    else
        o->window_w = (unsigned)(((unsigned long)o->window_h * num + den / 2) / den);
    if (!o->window_w)
        o->window_w = 640;
    if (!o->window_h)
        o->window_h = 480;

    s = env_get(get, "RECOMP_PRESENT_FILTER");
    o->present_filter = choose_filter(s, o);
    filter_set = o->present_filter >= 0;
    if (!filter_set) {
        o->present_filter = 0;
        snprintf(o->filter_name, sizeof o->filter_name, "nearest");
    }
    /*
     * Scale 1 + nearest + the window still at its created size takes the
     * stock copy, which plants the frame at the top left of a wider window.
     * Linear (or any non-nearest filter) takes the letterbox blit. A wider
     * aspect with scale 1 therefore promotes an unset filter to linear.
     * render.scale other than 1 already leaves that stock path.
     */
    if (strcmp(o->aspect, "4:3") != 0 && !filter_set && o->render_scale == 1) {
        o->present_filter = 1;
        snprintf(o->filter_name, sizeof o->filter_name, "linear");
        o->filter_promoted = 1;
    }

    o->fullscreen = choose_fullscreen(env_get(get, "RECOMP_PRESENT_FULLSCREEN"));
    s = env_get(get, "RECOMP_FPS60");
    o->fps60 = s && s[0] == '1';
    s = env_get(get, "RECOMP_PB_PIPELINE");
    o->pipeline = s && s[0] == '1';
    s = env_get(get, "RECOMP_PRESENT_VSYNC");
    o->vsync = s && s[0] == '1';
}

#if defined(_WIN32) && !defined(DISPLAY_HOST_TEST)
#include "kernel/nv2a_backend_common.h"
#include "video_config.h"
#include "video_menu.h"
#include "kernel/nv2a_tex_pack.h"
#include "platform/recomp_env.h"
#include "debug_text_host.h"
#include "mem_search_host.h"
#include <windows.h>

/* Texture packs are normally loaded by the toolkit's enhancements layer, which this build does not start.
 * RECOMP_TEXTURES_PACK=<file> (beside the exe unless absolute) loads one here. With RECOMP_VIDEO_ROW=1 and no
 * pack named, menu_labels.pack (tools/make_menu_labels.py: the VIDEO SETTINGS label) is used when present. */
static void display_load_pack(void)
{
    const char *p = getenv("RECOMP_TEXTURES_PACK"), *row = getenv("RECOMP_VIDEO_ROW");
    char path[MAX_PATH], err[128];
    int n, quiet = 0;
    DWORD len;
    char *slash;

    if (!p || !p[0]) {
        if (!row || row[0] != '1')
            return;
        p = "menu_labels.pack";
        quiet = 1;
    }
    if (!strcmp(p, "none"))
        return;
    if (p[0] == 0x5C || p[0] == '/' || (p[0] && p[1] == ':')) {
        snprintf(path, sizeof path, "%s", p);
    } else {
        len = GetModuleFileNameA(NULL, path, sizeof path);
        slash = len ? strrchr(path, 0x5C) : NULL;
        if (!slash)
            return;
        snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "%s", p);
    }
    if (quiet && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "[VIDEO-ROW] no %s; run tools/make_menu_labels.py for the VIDEO SETTINGS label\n", p);
        return;
    }
    n = nv2a_tex_pack_load(path, err, sizeof err);
    if (n < 0)
        fprintf(stderr, "[TEXPACK] %s: %s; no pack\n", path, err);
    else
        fprintf(stderr, "[TEXPACK] %s: %d entries\n", path, n);
}

/* Defined by patch_d3d11_window.py inside nv2a_pb_d3d11.c. Weak so an
 * unpatched toolkit still links; the pointer stays NULL and the window
 * stays 640x480. Smoke tests never look at this declaration. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
extern void (*breakdown_display_window_size_hook)(unsigned int *w, unsigned int *h);

static const char *display_source(const char *key)
{
    const char *e = getenv(key);
    if (e && *e)
        return "env";
    if (video_config_getenv(key))
        return "file";
    return "default";
}

static int exclusive_linked(void)
{
#if defined(__GNUC__) || defined(__clang__)
    return &breakdown_display_set_exclusive != 0;
#else
    return 1;
#endif
}

static const char *fullscreen_word(int mode)
{
    if (mode == DISPLAY_EXCLUSIVE)
        return "exclusive";
    if (mode == DISPLAY_BORDERLESS)
        return "borderless";
    return "windowed";
}

/* The D3D11 backend's init() calls this hook before its once-only check, and
 * d3d11_on_draw() calls init() on every draw, so it ran ~370 times a frame in
 * busy rooms (S8: seen in gdb stack samples of the nv2a-ack thread). Only the
 * first answer is used (the window is created once), so compute it once. */
static void display_apply_window(unsigned int *w, unsigned int *h)
{
    static int done;
    static unsigned int cw, ch;
    if (!done) {
        struct display_choice c;
        /* Same precedence as startup: a set RECOMP_* var beats the config file. */
        display_host_choose(display_getenv_layered, &c);
        cw = c.window_w;
        ch = c.window_h;
        done = 1;
    }
    if (w)
        *w = cw;
    if (h)
        *h = ch;
}

static const char *const s_speed_keys[] = { "RECOMP_FPS60", "RECOMP_PB_PIPELINE", "RECOMP_PRESENT_VSYNC" };
static int s_speed_from_launch[3];

int display_host_launch_set(const char *key)
{
    size_t k;
    const char *e;
    for (k = 0; k < sizeof s_speed_keys / sizeof s_speed_keys[0]; k++)
        if (key && strcmp(key, s_speed_keys[k]) == 0)
            return s_speed_from_launch[k];
    e = key ? getenv(key) : NULL;
    return e && e[0];
}

void display_host_init(void)
{
    struct display_choice c;
    struct nv2a_host_opts opts;
    char cfg[512];
    int st = -1;

    if (video_config_beside_exe(cfg, sizeof cfg) == 0)
        st = video_config_load(cfg);
    else
        cfg[0] = 0;
    if (cfg[0])
        fprintf(stderr, "[VIDEO] config %s (%s)\n", cfg,
                st == 1 ? "loaded" : st == 0 ? "not created yet" : "not read");
    /* Real environment first. video_config_getenv is the file, and it
     * returns char * so it matches display_getenv_fn on mingw. */
    display_host_set_sources(getenv, video_config_getenv);
    /* 60 FPS, pipelining and VSync are read by the 60 FPS override (src/recomp_manual.c, getenv) and by
     * the toolkit, which starts after this. A value saved in the file reaches them here, unless a real
     * environment variable is already set. The toolkit's own settings (recomp_env, e.g.
     * RECOMP_PRESENT_VSYNC) were snapshotted by recomp_env_init before the file was read, so the
     * snapshot is reloaded when a file value was copied in. */
    {
        size_t k;
        int copied = 0;
        for (k = 0; k < sizeof s_speed_keys / sizeof s_speed_keys[0]; k++) {
            const char *e = getenv(s_speed_keys[k]), *f = video_config_getenv(s_speed_keys[k]);
            s_speed_from_launch[k] = e && e[0];
            if (!s_speed_from_launch[k] && f && f[0]) {
                _putenv_s(s_speed_keys[k], f);
                copied = 1;
            }
        }
        if (copied)
            recomp_env_reload();
    }

    /* Address is NULL when the patched backend is not in this link. */
    if (&breakdown_display_window_size_hook)
        breakdown_display_window_size_hook = display_apply_window;
    else
        fprintf(stderr, "[DISPLAY] window hook not linked; "
                "run APPLY_DISPLAY_PATCHES.cmd and rebuild (window stays 640x480)\n");

    display_host_choose(display_getenv_layered, &c);
    memset(&opts, 0, sizeof opts);
    opts.render_scale = c.render_scale;
    opts.present_filter = c.present_filter;
    opts.fullscreen = c.fullscreen;
    /* Unpatched toolkit: any non-zero fullscreen is borderless at the
     * monitor size. Do not hand it 2, or exclusive would silently cover
     * the desktop. The saved word stays exclusive for the next launch. */
    if (opts.fullscreen == DISPLAY_EXCLUSIVE && !exclusive_linked()) {
        fprintf(stderr, "[DISPLAY] exclusive fullscreen needs the D3D11 patch;"
                " using borderless (run APPLY_DISPLAY_PATCHES.cmd and rebuild)\n");
        opts.fullscreen = DISPLAY_BORDERLESS;
    }
    nv2a_host_opts_set(&opts);
    display_load_pack();
    debug_text_host_start();
    mem_search_start();
    fprintf(stderr,
            "[DISPLAY] render.scale=%u%s present.filter=%s%s fullscreen=%d (%s)"
            " window=%ux%u aspect=%s%s (letterbox, 4:3 frame; hor+ is not in this pin)\n",
            c.render_scale, c.scale_clamped ? " (clamped to 1..4)" : "",
            c.filter_name,
            c.filter_promoted ? " (promoted so a wide window letterboxes)" : "",
            opts.fullscreen, fullscreen_word(opts.fullscreen),
            c.window_w, c.window_h, c.aspect,
            c.aspect_unknown ? " (unknown aspect, using 4:3)" : "");
    if (c.window_clamped)
        fprintf(stderr, "[DISPLAY] RECOMP_WINDOW_SCALE clamped to 1..6\n");
    fprintf(stderr,
            "[VIDEO] sources render=%s window=%s aspect=%s filter=%s fullscreen=%s\n",
            display_source("RECOMP_RENDER_SCALE"),
            display_source("RECOMP_WINDOW_SCALE"),
            display_source("RECOMP_DISPLAY_ASPECT"),
            display_source("RECOMP_PRESENT_FILTER"),
            display_source("RECOMP_PRESENT_FULLSCREEN"));
    video_menu_start();
}
#endif

#ifdef DISPLAY_HOST_TEST
static const char *s_env[8];
static const char *s_val[8];
static int s_n;

/* getenv's return is char *, not const char *. */
static char *test_get(const char *name)
{
    int i;
    for (i = 0; i < s_n; i++)
        if (strcmp(s_env[i], name) == 0)
            return (char *)s_val[i];
    return NULL;
}

static void setenv_test(const char *k, const char *v)
{
    s_env[s_n] = k;
    s_val[s_n] = v;
    s_n++;
}

/* Saved-file stand-in. Not the process environment. */
static const char *s_file_env[8];
static const char *s_file_val[8];
static int s_file_n;

static char *file_get(const char *name)
{
    int i;
    for (i = 0; i < s_file_n; i++)
        if (strcmp(s_file_env[i], name) == 0)
            return (char *)s_file_val[i];
    return NULL;
}

static void setfile_test(const char *k, const char *v)
{
    s_file_env[s_file_n] = k;
    s_file_val[s_file_n] = v;
    s_file_n++;
}

static int g_fail;

static void expect(const char *name, int cond)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        g_fail++;
    } else {
        printf("ok %s\n", name);
    }
}

int main(void)
{
    struct display_choice c;

    s_file_n = 0;
    setfile_test("RECOMP_RENDER_SCALE", "4");
    setfile_test("RECOMP_WINDOW_SCALE", "3");
    setfile_test("RECOMP_DISPLAY_ASPECT", "16:9");
    setfile_test("RECOMP_PRESENT_FILTER", "integer");
    setfile_test("RECOMP_PRESENT_FULLSCREEN", "1");

    s_n = 0;
    display_host_choose(test_get, &c);
    expect("stock scale", c.render_scale == 1);
    expect("stock filter", c.present_filter == 0);
    expect("stock window", c.window_w == 640 && c.window_h == 480);
    expect("stock aspect", strcmp(c.aspect, "4:3") == 0);
    expect("stock fullscreen", c.fullscreen == 0);

    s_n = 0;
    setenv_test("RECOMP_RENDER_SCALE", "2");
    setenv_test("RECOMP_WINDOW_SCALE", "2");
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:9");
    display_host_choose(test_get, &c);
    expect("16:9 scale", c.render_scale == 2);
    expect("16:9 size", c.window_w == 1707 && c.window_h == 960);
    expect("16:9 not promoted", c.filter_promoted == 0 && c.present_filter == 0);

    s_n = 0;
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:9");
    display_host_choose(test_get, &c);
    expect("promote linear", c.filter_promoted == 1 && c.present_filter == 1);
    expect("16:9 height 480", c.window_w == 853 && c.window_h == 480);

    s_n = 0;
    setenv_test("RECOMP_RENDER_SCALE", "9");
    setenv_test("RECOMP_WINDOW_SCALE", "0");
    setenv_test("RECOMP_DISPLAY_ASPECT", "nope");
    display_host_choose(test_get, &c);
    expect("scale clamp", c.render_scale == 4 && c.scale_clamped);
    expect("window clamp", c.window_w == 640 && c.window_h == 480 && c.window_clamped);
    expect("unknown aspect", c.aspect_unknown && strcmp(c.aspect, "4:3") == 0);

    s_n = 0;
    setenv_test("RECOMP_WINDOW_SCALE", "2");
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:10");
    display_host_choose(test_get, &c);
    expect("16:10", c.window_w == 1536 && c.window_h == 960);

    s_n = 0;
    setenv_test("RECOMP_DISPLAY_ASPECT", "21:9");
    display_host_choose(test_get, &c);
    expect("21:9", c.window_w == 1120 && c.window_h == 480);

    s_n = 0;
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:9");
    setenv_test("RECOMP_PRESENT_FILTER", "nearest");
    display_host_choose(test_get, &c);
    expect("explicit nearest kept", c.present_filter == 0 && c.filter_promoted == 0);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FILTER", "integer");
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "1");
    display_host_choose(test_get, &c);
    expect("integer", c.present_filter == 2);
    expect("fullscreen", c.fullscreen == 1);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "exclusive");
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:9");
    setenv_test("RECOMP_WINDOW_SCALE", "2");
    display_host_choose(test_get, &c);
    expect("exclusive word", c.fullscreen == DISPLAY_EXCLUSIVE);
    expect("exclusive keeps letterbox size", c.window_w == 1707 && c.window_h == 960);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "2");
    display_host_choose(test_get, &c);
    expect("exclusive digit", c.fullscreen == 2);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "borderless");
    display_host_choose(test_get, &c);
    expect("borderless word", c.fullscreen == DISPLAY_BORDERLESS);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "true");
    display_host_choose(test_get, &c);
    expect("true is borderless", c.fullscreen == 1);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "nope");
    display_host_choose(test_get, &c);
    expect("unknown fullscreen is windowed", c.fullscreen == 0);

    /* File-backed defaults: a second getenv used only when the env one is
     * unset or empty. An explicit env value, including fullscreen "0", wins. */
    s_n = 0;
    display_host_set_sources(test_get, NULL);
    display_host_choose(display_getenv_layered, &c);
    expect("layered without file is stock",
           c.render_scale == 1 && c.window_w == 640 && c.window_h == 480
           && strcmp(c.aspect, "4:3") == 0 && c.fullscreen == 0
           && c.present_filter == 0);

    s_n = 0;
    setenv_test("RECOMP_RENDER_SCALE", "2");
    setenv_test("RECOMP_DISPLAY_ASPECT", "4:3");
    display_host_set_sources(test_get, file_get);
    display_host_choose(display_getenv_layered, &c);
    expect("env beats file scale", c.render_scale == 2);
    expect("env beats file aspect",
           strcmp(c.aspect, "4:3") == 0 && c.window_w == 1920 && c.window_h == 1440);
    expect("file filter when env unset", c.present_filter == 2 && c.filter_promoted == 0);
    expect("file fullscreen when env unset", c.fullscreen == 1);
    expect("file window scale", c.window_h == 1440);

    s_n = 0;
    display_host_set_sources(test_get, file_get);
    display_host_choose(display_getenv_layered, &c);
    expect("file when env unset", c.render_scale == 4 && strcmp(c.aspect, "16:9") == 0);
    expect("file 16:9 size", c.window_w == 2560 && c.window_h == 1440);

    s_n = 0;
    setenv_test("RECOMP_PRESENT_FULLSCREEN", "0");
    display_host_set_sources(test_get, file_get);
    display_host_choose(display_getenv_layered, &c);
    expect("explicit fullscreen 0 beats file", c.fullscreen == 0);
    expect("file still supplies scale", c.render_scale == 4);

    s_n = 0;
    setenv_test("RECOMP_RENDER_SCALE", "");
    display_host_set_sources(test_get, file_get);
    display_host_choose(display_getenv_layered, &c);
    expect("empty env falls through to file", c.render_scale == 4);

    s_n = 0;
    setenv_test("RECOMP_RENDER_SCALE", "1");
    setenv_test("RECOMP_DISPLAY_ASPECT", "16:9");
    setenv_test("RECOMP_PRESENT_FILTER", "nearest");
    display_host_set_sources(test_get, file_get);
    display_host_choose(display_getenv_layered, &c);
    expect("explicit nearest still not promoted",
           c.present_filter == 0 && c.filter_promoted == 0 && c.render_scale == 1
           && c.window_w == 2560 && c.window_h == 1440);

    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    puts("all passed");
    return 0;
}
#endif
