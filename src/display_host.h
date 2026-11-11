/*
 * display_host.h - output size for the D3D11 window.
 *
 * The Xbox frame stays 4:3. These knobs scale that frame and, optionally,
 * put it in a wider window with bars, or ask the display for an exclusive
 * mode at that window size. They do not render a wider view (Hor+).
 * docs/widescreen-hires.md
 */
#ifndef DISPLAY_HOST_H
#define DISPLAY_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

/* fullscreen in struct display_choice, and nv2a_host_opts.fullscreen. */
enum {
    DISPLAY_WINDOWED = 0,
    DISPLAY_BORDERLESS = 1, /* desktop-sized popup; the pin's old fullscreen */
    DISPLAY_EXCLUSIVE = 2   /* SetFullscreenState on the desktop mode, 4:3 letterboxed */
};

struct display_choice {
    unsigned render_scale;     /* 1..4, guest render targets */
    int      present_filter;   /* 0 nearest, 1 linear, 2 integer */
    int      fullscreen;       /* DISPLAY_WINDOWED / BORDERLESS / EXCLUSIVE */
    unsigned window_w;         /* client, before borderless replaces it */
    unsigned window_h;
    int      scale_clamped;
    int      window_clamped;
    int      aspect_unknown;
    int      filter_promoted;  /* nearest would not letterbox at scale 1 */
    char     aspect[8];        /* "4:3", "16:9", "16:10", "21:9" */
    char     filter_name[12];
};

/* Same type as getenv (char *getenv(const char *)). A const-returning
 * getter does not match, and mingw-clang rejects that pointer conversion.
 * `get` is getenv, or a test double. Never returns a zero window. */
typedef char *(*display_getenv_fn)(const char *name);

void display_host_choose(display_getenv_fn get, struct display_choice *out);

/* Env wins. `file` is used only when `env` is missing or returns an empty
 * string. Either pointer may be NULL. Not re-entrant: the two pointers are
 * process-wide, and display_getenv_layered reads them. */
void display_host_set_sources(display_getenv_fn env, display_getenv_fn file);
char *display_getenv_layered(const char *name);

/* After recomp_env_init, before the guest. Sets nv2a_host_opts, and the
 * optional D3D11 window hook when the toolkit patch defined it. */
void display_host_init(void);

/* Defined by patch_d3d11_exclusive.py. Weak on mingw-clang: an unpatched
 * toolkit leaves the address NULL and exclusive falls back to borderless.
 * A const-qualified or differently typed symbol would not match. Call it
 * only after taking the address; a direct call is not optional. */
#if defined(_WIN32)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
extern void breakdown_display_set_exclusive(int on);
/* 1 while DXGI exclusive is on. Same weak rule: NULL address on an unpatched toolkit. */
extern int breakdown_display_exclusive_active(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_HOST_H */
