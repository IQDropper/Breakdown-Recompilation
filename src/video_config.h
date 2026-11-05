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
    int filter;            /* 0 nearest, 1 linear, 2 integer */
    int borderless;        /* 0 windowed, 1 borderless (not exclusive) */
};

enum {
    VIDEO_ROW_SCALE = 0,
    VIDEO_ROW_WINDOW,
    VIDEO_ROW_ASPECT,
    VIDEO_ROW_FILTER,
    VIDEO_ROW_MODE,
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
void video_settings_clamp(struct video_settings *s);
void video_settings_from_choice(const struct display_choice *c, struct video_settings *s);

int video_settings_aspect_index(const char *name);
const char *video_settings_aspect_name(int index);
const char *video_settings_filter_name(int index);

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
