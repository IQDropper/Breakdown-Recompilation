/*
 * display_host.h - output size for the D3D11 window.
 *
 * The Xbox frame stays 4:3. These knobs scale that frame and, optionally,
 * put it in a wider window with bars. They do not render a wider view.
 * docs/widescreen-hires.md
 */
#ifndef DISPLAY_HOST_H
#define DISPLAY_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

struct display_choice {
    unsigned render_scale;     /* 1..4, guest render targets */
    int      present_filter;   /* 0 nearest, 1 linear, 2 integer */
    int      fullscreen;       /* borderless desktop */
    unsigned window_w;         /* windowed client, before fullscreen */
    unsigned window_h;
    int      scale_clamped;
    int      window_clamped;
    int      aspect_unknown;
    int      filter_promoted;  /* nearest would not letterbox at scale 1 */
    char     aspect[8];        /* "4:3", "16:9", "16:10", "21:9" */
    char     filter_name[12];
};

/* `get` is getenv, or a test double. Never returns a zero window. */
void display_host_choose(const char *(*get)(const char *name),
                         struct display_choice *out);

/* After recomp_env_init, before the guest. Sets nv2a_host_opts, and the
 * optional D3D11 window hook when the toolkit patch defined it. */
void display_host_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_HOST_H */
