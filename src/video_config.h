/*
 * video_config.h - saved Video options and the menu's value model.
 *
 * The file is breakdown_video.cfg beside the exe (build-win\ when that is
 * where breakdown_recomp.exe lives). A set RECOMP_* variable overrides the
 * matching line. An empty or missing variable does not. See docs/video-options.md.
 */
#ifndef VIDEO_CONFIG_H
#define VIDEO_CONFIG_H

#include "display_host.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_CONFIG_FILENAME "breakdown_video.cfg"

struct video_settings {
    unsigned render_scale; /* 1..4, RECOMP_RENDER_SCALE */
    unsigned window_scale; /* 1..6, RECOMP_WINDOW_SCALE */
    int aspect;            /* 0 4:3, 1 16:9, 2 16:10, 3 21:9 */
    int horplus;           /* 0/1, RECOMP_HORPLUS: wider view at a wide aspect instead of bars (next launch) */
    int filter;            /* 0 nearest, 1 linear, 2 integer */
    int borderless;        /* 0 windowed, 1 borderless, 2 exclusive */
    int fps60;             /* 0/1, RECOMP_FPS60: gameplay at 60 FPS (applies at once) */
    int pipeline;          /* 0/1, RECOMP_PB_PIPELINE: game doesn't wait for the renderer (next launch) */
    int vsync;             /* 0/1, RECOMP_PRESENT_VSYNC: wait for the display, no tearing (applies at once) */
    int a11y_repeat;       /* A11Y_REPEAT_*, RECOMP_A11Y_REPEAT: hold a button = 10 presses/s (applies at once) */
    int a11y_toggle;       /* A11Y_TOGGLE_*, RECOMP_A11Y_TOGGLE: a trigger latches on a press (applies at once) */
    int outfit;            /* skins.h choice, RECOMP_HERO_OUTFIT: hero outfit (next spawn) */
    int content;           /* content_mode.h, RECOMP_CONTENT: 0 classic, 1 new content (S10.15) */
    int text_size;         /* 0 100%, 1 150%, 2 200%, RECOMP_PC_TEXT_SIZE: PC page text (applies at once) */
    int mouse_speed;       /* VIDEO_MOUSE_SPEEDS index (100% = 1200), RECOMP_MOUSE_SENS (applies at once) */
    int mouse_invert;      /* 0/1, RECOMP_MOUSE_INVERT (applies at once) */
    int deadzone;          /* VIDEO_DEADZONES index (0 = off), RECOMP_PAD_DEADZONE (applies at once) */
    char keymap[512];      /* keymap.h text, RECOMP_KEYMAP: remapped keys (applies at once) */
};

enum {
    VIDEO_ROW_SCALE = 0,
    VIDEO_ROW_WINDOW,
    VIDEO_ROW_ASPECT,
    VIDEO_ROW_WIDE,
    VIDEO_ROW_FILTER,
    VIDEO_ROW_MODE,
    VIDEO_ROW_FPS60,
    VIDEO_ROW_PIPELINE,
    VIDEO_ROW_VSYNC,
    VIDEO_ROW_REPEAT,
    VIDEO_ROW_TOGGLE,
    VIDEO_ROW_OUTFIT,
    VIDEO_ROW_CONTENT,
    VIDEO_ROW_TEXT,
    VIDEO_ROW_MOUSE,
    VIDEO_ROW_INVERT,
    VIDEO_ROW_DEADZONE,
    VIDEO_ROW_KEYS,
    VIDEO_ROW_APPLY,
    VIDEO_ROW_OK,
    VIDEO_ROW_CANCEL,
    VIDEO_ROW_COUNT
};

/* dir + filename. 0 on success, -1 if it does not fit. A Windows dir
 * (backslash, no slash) keeps backslashes. */
int video_config_join(const char *dir, char *out, size_t out_n);

void video_config_reset(void);

/* 1 if the file was opened, 0 if it is missing, -1 if path is NULL.
 * A missing file clears anything previously loaded. Unknown lines are
 * ignored. The stored text is what the file said, not a clamped value. */
int video_config_load(const char *path);

/* Matches display_getenv_fn: char *, NULL when that key was not in the file. */
char *video_config_getenv(const char *name);

/* Writes the five keys from clamped settings. 0 on success, -1 on error.
 * On success the in-memory file image matches what was written. */
int video_config_save(const char *path, const struct video_settings *s);

#ifdef _WIN32
/* Folder of this exe + VIDEO_CONFIG_FILENAME. 0 on success, -1 otherwise. */
int video_config_beside_exe(char *out, size_t out_n);
#endif

void video_settings_stock(struct video_settings *s);
/* The finalized "Recommended" preset (Foster, 2026-11-13; docs/SETTINGS_LAB.md): internal 4x, linear, 16:9 with
 * Hor+, 60 FPS, don't wait for the renderer, VSync. Picture and speed rows only: window size and mode, controls,
 * accessibility and outfit stay as they are. */
void video_settings_recommended(struct video_settings *s);
/* What breakdown_video.cfg stores for RECOMP_HORPLUS: the aspect ("16:9") when Hor+ is on at a wide aspect, else "off". */
const char *video_settings_horplus_token(const struct video_settings *s);
void video_settings_clamp(struct video_settings *s);
void video_settings_from_choice(const struct display_choice *c, struct video_settings *s);

int video_settings_aspect_index(const char *name);
const char *video_settings_aspect_name(int index);
const char *video_settings_filter_name(int index);
/* Text size choice -> percent (100, 150, 200) and back (unknown text -> 0, i.e. 100%). */
#define VIDEO_TEXT_SIZES 3
int video_text_size_percent(int index);
int video_text_size_index(const char *s);
/* A11Y-1 controls: mouse speed percent <-> RECOMP_MOUSE_SENS units (100% = 1200), stick dead zone percent <->
 * RECOMP_PAD_DEADZONE units (of 32767). Index from units picks the nearest choice. */
#define VIDEO_MOUSE_SPEEDS 6
#define VIDEO_MOUSE_DEFAULT 2
#define VIDEO_DEADZONES 4
int video_mouse_percent(int index);
int video_mouse_units(int index);
int video_mouse_index(int units);
int video_deadzone_percent(int index);
int video_deadzone_units(int index);
int video_deadzone_index(int units);
/* "0", "1", or "exclusive". What breakdown_video.cfg stores. */
const char *video_settings_fullscreen_token(int mode);

const char *video_row_title(int row);
/* dir is -1 or +1. Action rows do not change. Returns 1 if a value changed. */
int video_settings_nudge(struct video_settings *s, int row, int dir);
char *video_settings_value(const struct video_settings *s, int row, char *buf, size_t n);
const char *video_row_detail(const struct video_settings *s, int row);
/* Window size, aspect, and window mode resize the D3D11 window. Scale and
 * the present filter go through nv2a_host_opts and do not need the HWND. */
int video_row_needs_window(int row);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_CONFIG_H */
