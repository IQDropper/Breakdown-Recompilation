/*
 * video_config.c - breakdown_video.cfg and the Video menu's rows.
 *
 * The present path still reads RECOMP_* through display_host_choose.
 * This file only stores the strings and the labels the menu shows.
 */
#include "video_config.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

enum {
    KEY_SCALE = 0,
    KEY_WINDOW,
    KEY_ASPECT,
    KEY_FILTER,
    KEY_FULL,
    KEY_COUNT
};

static const char *const s_keys[KEY_COUNT] = {
    "RECOMP_RENDER_SCALE",
    "RECOMP_WINDOW_SCALE",
    "RECOMP_DISPLAY_ASPECT",
    "RECOMP_PRESENT_FILTER",
    "RECOMP_PRESENT_FULLSCREEN",
};

static const char *const s_aspects[] = { "4:3", "16:9", "16:10", "21:9" };
static const char *const s_filters[] = { "nearest", "linear", "integer" };
static const char *const s_filter_labels[] = { "Nearest", "Linear", "Integer" };

static char s_val[KEY_COUNT][16];
static int s_have[KEY_COUNT];

static void trim_inplace(char *s)
{
    char *start = s;
    char *end;

    while (*start == ' ' || *start == '\t')
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
}

static int key_index(const char *name)
{
    int i;
    for (i = 0; i < KEY_COUNT; i++) {
        if (strcmp(name, s_keys[i]) == 0)
            return i;
    }
    return -1;
}

static void parse_line(char *line)
{
    char *eq;
    int i;
    size_t n;

    trim_inplace(line);
    if (!line[0] || line[0] == '#' || line[0] == ';')
        return;
    eq = strchr(line, '=');
    if (!eq)
        return;
    *eq = 0;
    trim_inplace(line);
    trim_inplace(eq + 1);
    n = strlen(eq + 1);
    if (!line[0] || n == 0 || n >= sizeof s_val[0])
        return;
    i = key_index(line);
    if (i < 0)
        return;
    memcpy(s_val[i], eq + 1, n + 1);
    s_have[i] = 1;
}

int video_config_join(const char *dir, char *out, size_t out_n)
{
    size_t len;
    int n;
    char sep;

    if (!out || out_n == 0)
        return -1;
    out[0] = 0;
    if (!dir || !dir[0]) {
        n = snprintf(out, out_n, "%s", VIDEO_CONFIG_FILENAME);
        return (n < 0 || (size_t)n >= out_n) ? -1 : 0;
    }
    len = strlen(dir);
    sep = (strchr(dir, '\\') && !strchr(dir, '/')) ? '\\' : '/';
    if (dir[len - 1] == '/' || dir[len - 1] == '\\')
        n = snprintf(out, out_n, "%s%s", dir, VIDEO_CONFIG_FILENAME);
    else
        n = snprintf(out, out_n, "%s%c%s", dir, sep, VIDEO_CONFIG_FILENAME);
    return (n < 0 || (size_t)n >= out_n) ? -1 : 0;
}

void video_config_reset(void)
{
    int i;
    for (i = 0; i < KEY_COUNT; i++) {
        s_val[i][0] = 0;
        s_have[i] = 0;
    }
}

int video_config_load(const char *path)
{
    FILE *f;
    char line[256];

    video_config_reset();
    if (!path)
        return -1;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    while (fgets(line, (int)sizeof line, f))
        parse_line(line);
    fclose(f);
    return 1;
}

char *video_config_getenv(const char *name)
{
    int i;
    if (!name)
        return NULL;
    i = key_index(name);
    if (i < 0 || !s_have[i])
        return NULL;
    return s_val[i];
}

static void store_settings(const struct video_settings *s)
{
    struct video_settings c = *s;
    video_settings_clamp(&c);
    snprintf(s_val[KEY_SCALE], sizeof s_val[KEY_SCALE], "%u", c.render_scale);
    snprintf(s_val[KEY_WINDOW], sizeof s_val[KEY_WINDOW], "%u", c.window_scale);
    snprintf(s_val[KEY_ASPECT], sizeof s_val[KEY_ASPECT], "%s",
             video_settings_aspect_name(c.aspect));
    snprintf(s_val[KEY_FILTER], sizeof s_val[KEY_FILTER], "%s",
             video_settings_filter_name(c.filter));
    snprintf(s_val[KEY_FULL], sizeof s_val[KEY_FULL], "%s",
             video_settings_fullscreen_token(c.borderless));
    s_have[KEY_SCALE] = 1;
    s_have[KEY_WINDOW] = 1;
    s_have[KEY_ASPECT] = 1;
    s_have[KEY_FILTER] = 1;
    s_have[KEY_FULL] = 1;
}

int video_config_save(const char *path, const struct video_settings *in)
{
    struct video_settings s;
    char tmp[4096];
    FILE *f;
    int n;

    if (!path || !path[0] || !in)
        return -1;
    n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof tmp)
        return -1;
    s = *in;
    video_settings_clamp(&s);
    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    if (fprintf(f,
                "# Breakdown video options\n"
                "# Beside the exe, named %s\n"
                "# A set RECOMP_* environment variable overrides that line.\n"
                "# run_native.cmd sets some of these, so it ignores this file\n"
                "# until those variables are unset.\n"
                "# RECOMP_PRESENT_FULLSCREEN: 0 windowed, 1 borderless, exclusive.\n"
                "# Hor+ is not a setting on this toolkit pin.\n"
                "RECOMP_RENDER_SCALE=%u\n"
                "RECOMP_WINDOW_SCALE=%u\n"
                "RECOMP_DISPLAY_ASPECT=%s\n"
                "RECOMP_PRESENT_FILTER=%s\n"
                "RECOMP_PRESENT_FULLSCREEN=%s\n",
                VIDEO_CONFIG_FILENAME,
                s.render_scale, s.window_scale,
                video_settings_aspect_name(s.aspect),
                video_settings_filter_name(s.filter),
                video_settings_fullscreen_token(s.borderless)) < 0) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -1;
        }
    }
    store_settings(&s);
    return 0;
}

#ifdef _WIN32
int video_config_beside_exe(char *out, size_t out_n)
{
    char mod[MAX_PATH];
    DWORD got;
    char *slash;

    if (!out || out_n == 0)
        return -1;
    got = GetModuleFileNameA(NULL, mod, MAX_PATH);
    if (got == 0 || got >= MAX_PATH)
        return -1;
    slash = strrchr(mod, '\\');
    if (!slash)
        slash = strrchr(mod, '/');
    if (slash)
        *slash = 0;
    else
        mod[0] = 0;
    return video_config_join(mod[0] ? mod : ".", out, out_n);
}
#endif

void video_settings_stock(struct video_settings *s)
{
    if (!s)
        return;
    s->render_scale = 1;
    s->window_scale = 1;
    s->aspect = 0;
    s->filter = 0;
    s->borderless = 0;
}

void video_settings_clamp(struct video_settings *s)
{
    if (!s)
        return;
    if (s->render_scale < 1)
        s->render_scale = 1;
    if (s->render_scale > 4)
        s->render_scale = 4;
    if (s->window_scale < 1)
        s->window_scale = 1;
    if (s->window_scale > 6)
        s->window_scale = 6;
    if (s->aspect < 0)
        s->aspect = 0;
    if (s->aspect > 3)
        s->aspect = 3;
    if (s->filter < 0)
        s->filter = 0;
    if (s->filter > 2)
        s->filter = 2;
    if (s->borderless < DISPLAY_WINDOWED)
        s->borderless = DISPLAY_WINDOWED;
    if (s->borderless > DISPLAY_EXCLUSIVE)
        s->borderless = DISPLAY_EXCLUSIVE;
}

int video_settings_aspect_index(const char *name)
{
    int i;
    if (!name)
        return -1;
    for (i = 0; i < 4; i++) {
        if (strcmp(name, s_aspects[i]) == 0)
            return i;
    }
    return -1;
}

const char *video_settings_aspect_name(int index)
{
    if (index < 0 || index > 3)
        return s_aspects[0];
    return s_aspects[index];
}

const char *video_settings_filter_name(int index)
{
    if (index < 0 || index > 2)
        return s_filters[0];
    return s_filters[index];
}

const char *video_settings_fullscreen_token(int mode)
{
    if (mode == DISPLAY_EXCLUSIVE)
        return "exclusive";
    if (mode == DISPLAY_BORDERLESS)
        return "1";
    return "0";
}

static const char *window_mode_label(int mode)
{
    if (mode == DISPLAY_EXCLUSIVE)
        return "Exclusive";
    if (mode == DISPLAY_BORDERLESS)
        return "Borderless";
    return "Windowed";
}

void video_settings_from_choice(const struct display_choice *c, struct video_settings *s)
{
    unsigned ws;
    int aspect;

    video_settings_stock(s);
    if (!c || !s)
        return;
    s->render_scale = c->render_scale ? c->render_scale : 1;
    ws = c->window_h ? (c->window_h + 240u) / 480u : 1u;
    s->window_scale = ws ? ws : 1u;
    aspect = video_settings_aspect_index(c->aspect);
    s->aspect = aspect < 0 ? 0 : aspect;
    if (c->present_filter >= 0 && c->present_filter <= 2)
        s->filter = c->present_filter;
    s->borderless = c->fullscreen;
    video_settings_clamp(s);
}

const char *video_row_title(int row)
{
    static const char *const titles[VIDEO_ROW_COUNT] = {
        "Internal resolution",
        "Window size",
        "Aspect",
        "Present filter",
        "Window mode",
        "Apply",
        "OK",
        "Cancel",
    };
    if (row < 0 || row >= VIDEO_ROW_COUNT)
        return "";
    return titles[row];
}

static int wrap_index(int i, int n, int dir)
{
    if (dir > 0)
        i++;
    else
        i--;
    if (i < 0)
        i = n - 1;
    if (i >= n)
        i = 0;
    return i;
}

int video_settings_nudge(struct video_settings *s, int row, int dir)
{
    unsigned before_u;
    int before;

    if (!s || !dir)
        return 0;
    switch (row) {
    case VIDEO_ROW_SCALE:
        before_u = s->render_scale;
        if (dir > 0 && s->render_scale < 4)
            s->render_scale++;
        else if (dir < 0 && s->render_scale > 1)
            s->render_scale--;
        return s->render_scale != before_u;
    case VIDEO_ROW_WINDOW:
        before_u = s->window_scale;
        if (dir > 0 && s->window_scale < 6)
            s->window_scale++;
        else if (dir < 0 && s->window_scale > 1)
            s->window_scale--;
        return s->window_scale != before_u;
    case VIDEO_ROW_ASPECT:
        before = s->aspect;
        s->aspect = wrap_index(s->aspect, 4, dir);
        return s->aspect != before;
    case VIDEO_ROW_FILTER:
        before = s->filter;
        s->filter = wrap_index(s->filter, 3, dir);
        return s->filter != before;
    case VIDEO_ROW_MODE:
        before = s->borderless;
        s->borderless = wrap_index(s->borderless, 3, dir);
        return s->borderless != before;
    default:
        return 0;
    }
}

char *video_settings_value(const struct video_settings *s, int row, char *buf, size_t n)
{
    if (!buf || n == 0)
        return buf;
    buf[0] = 0;
    if (!s)
        return buf;
    switch (row) {
    case VIDEO_ROW_SCALE:
        snprintf(buf, n, "%ux", s->render_scale);
        break;
    case VIDEO_ROW_WINDOW:
        snprintf(buf, n, "%ux", s->window_scale);
        break;
    case VIDEO_ROW_ASPECT:
        snprintf(buf, n, "%s", video_settings_aspect_name(s->aspect));
        break;
    case VIDEO_ROW_FILTER:
        if (s->filter >= 0 && s->filter <= 2)
            snprintf(buf, n, "%s", s_filter_labels[s->filter]);
        break;
    case VIDEO_ROW_MODE:
        snprintf(buf, n, "%s", window_mode_label(s->borderless));
        break;
    default:
        break;
    }
    return buf;
}

const char *video_row_detail(const struct video_settings *s, int row)
{
    switch (row) {
    case VIDEO_ROW_SCALE:
        return "1x is the Xbox internal size. Higher costs more. Changes on the next frame.";
    case VIDEO_ROW_WINDOW:
        return "Height is 480 times this. Width follows the aspect. Resizes the window now.";
    case VIDEO_ROW_ASPECT:
        return "Letterbox the 4:3 frame. Hor+ is not available on this toolkit pin.";
    case VIDEO_ROW_FILTER:
        if (s && s->render_scale <= 1 && s->aspect != 0 && s->filter == 0)
            return "Nearest at internal 1x in a wide window keeps the image at the top left. "
                   "Linear or integer centres it.";
        return "Nearest, linear, or integer. Changes on the next frame.";
    case VIDEO_ROW_MODE:
        return "Windowed, borderless on the monitor, or exclusive at this window size. "
               "The 4:3 frame stays letterboxed. Hor+ is not available.";
    case VIDEO_ROW_APPLY:
        return "Save breakdown_video.cfg beside the exe. This menu stays open.";
    case VIDEO_ROW_OK:
        return "Save and close.";
    case VIDEO_ROW_CANCEL:
        return "Undo previews and close. The file stays as it was.";
    default:
        return "";
    }
}

int video_row_needs_window(int row)
{
    return row == VIDEO_ROW_WINDOW || row == VIDEO_ROW_ASPECT || row == VIDEO_ROW_MODE;
}
