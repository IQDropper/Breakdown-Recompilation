/*
 * video_config.c - breakdown_video.cfg and the Video menu's rows.
 *
 * The present path still reads RECOMP_* through display_host_choose.
 * This file only stores the strings and the labels the menu shows.
 */
#include "video_config.h"
#include "a11y_input.h"
#include "skins.h"
#include "keymap.h"

#include <stdio.h>
#include <stdlib.h>
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
    KEY_FPS60,
    KEY_PIPE,
    KEY_VSYNC,
    KEY_REPEAT,
    KEY_TOGGLE,
    KEY_OUTFIT,
    KEY_TEXT,
    KEY_MOUSE,
    KEY_INVERT,
    KEY_DEADZONE,
    KEY_KEYMAP,
    KEY_HORPLUS,
    KEY_COUNT
};

static const char *const s_keys[KEY_COUNT] = {
    "RECOMP_RENDER_SCALE",
    "RECOMP_WINDOW_SCALE",
    "RECOMP_DISPLAY_ASPECT",
    "RECOMP_PRESENT_FILTER",
    "RECOMP_PRESENT_FULLSCREEN",
    "RECOMP_FPS60",
    "RECOMP_PB_PIPELINE",
    "RECOMP_PRESENT_VSYNC",
    "RECOMP_A11Y_REPEAT",
    "RECOMP_A11Y_TOGGLE",
    "RECOMP_HERO_OUTFIT",
    "RECOMP_PC_TEXT_SIZE",
    "RECOMP_MOUSE_SENS",
    "RECOMP_MOUSE_INVERT",
    "RECOMP_PAD_DEADZONE",
    "RECOMP_KEYMAP",
    "RECOMP_HORPLUS",
};

static const char *const s_aspects[] = { "4:3", "16:9", "16:10", "21:9" };
static const char *const s_filters[] = { "nearest", "linear", "integer" };
static const char *const s_filter_labels[] = { "Nearest", "Linear", "Integer" };

static char s_val[KEY_COUNT][512];   /* RECOMP_KEYMAP needs the room */
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
    char line[640];

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
    snprintf(s_val[KEY_FPS60], sizeof s_val[KEY_FPS60], "%d", c.fps60 ? 1 : 0);
    snprintf(s_val[KEY_PIPE], sizeof s_val[KEY_PIPE], "%d", c.pipeline ? 1 : 0);
    snprintf(s_val[KEY_VSYNC], sizeof s_val[KEY_VSYNC], "%d", c.vsync ? 1 : 0);
    snprintf(s_val[KEY_REPEAT], sizeof s_val[KEY_REPEAT], "%s", a11y_repeat_token(c.a11y_repeat));
    snprintf(s_val[KEY_TOGGLE], sizeof s_val[KEY_TOGGLE], "%s", a11y_toggle_token(c.a11y_toggle));
    snprintf(s_val[KEY_OUTFIT], sizeof s_val[KEY_OUTFIT], "%s", skins_choice_token(c.outfit));
    snprintf(s_val[KEY_TEXT], sizeof s_val[KEY_TEXT], "%d", video_text_size_percent(c.text_size));
    snprintf(s_val[KEY_MOUSE], sizeof s_val[KEY_MOUSE], "%d", video_mouse_units(c.mouse_speed));
    snprintf(s_val[KEY_INVERT], sizeof s_val[KEY_INVERT], "%d", c.mouse_invert ? 1 : 0);
    snprintf(s_val[KEY_DEADZONE], sizeof s_val[KEY_DEADZONE], "%d", video_deadzone_units(c.deadzone));
    snprintf(s_val[KEY_KEYMAP], sizeof s_val[KEY_KEYMAP], "%s", c.keymap);
    snprintf(s_val[KEY_HORPLUS], sizeof s_val[KEY_HORPLUS], "%s", video_settings_horplus_token(&c));
    s_have[KEY_SCALE] = 1;
    s_have[KEY_WINDOW] = 1;
    s_have[KEY_ASPECT] = 1;
    s_have[KEY_FILTER] = 1;
    s_have[KEY_FULL] = 1;
    s_have[KEY_FPS60] = 1;
    s_have[KEY_PIPE] = 1;
    s_have[KEY_VSYNC] = 1;
    s_have[KEY_REPEAT] = 1;
    s_have[KEY_TOGGLE] = 1;
    s_have[KEY_OUTFIT] = 1;
    s_have[KEY_TEXT] = 1;
    s_have[KEY_MOUSE] = 1;
    s_have[KEY_INVERT] = 1;
    s_have[KEY_DEADZONE] = 1;
    s_have[KEY_KEYMAP] = c.keymap[0] != 0;
    s_have[KEY_HORPLUS] = 1;
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
                "# RECOMP_HORPLUS: off, or the aspect (16:9 ...) = a wider view instead of bars\n"
                "# (takes effect on the next launch).\n"
                "# RECOMP_FPS60: 1 = gameplay at 60 FPS. RECOMP_PB_PIPELINE: 1 = the game\n"
                "# does not wait for the renderer (takes effect on the next launch).\n"
                "# RECOMP_PRESENT_VSYNC: 1 = wait for the display (no tearing).\n"
                "# RECOMP_A11Y_REPEAT: off, a, b, x, y or abxy = holding that button presses\n"
                "# it 10 times a second. RECOMP_A11Y_TOGGLE: off, rt, lt or both = one press\n"
                "# of that trigger holds it, the next press lets go.\n"
                "# RECOMP_HERO_OUTFIT: game, normal, t or researcher (story outfits still win;\n"
                "# shows from the next time the hero appears).\n"
                "# RECOMP_PC_TEXT_SIZE: 100, 150 or 200 = text size on this PC page.\n"
                "# RECOMP_MOUSE_SENS: mouse look speed (1200 = 100%%). RECOMP_MOUSE_INVERT: 1 = up is down.\n"
                "# RECOMP_PAD_DEADZONE: stick dead zone, 0..32767. RECOMP_KEYMAP: remapped keys,\n"
                "# CONTROL=KEY|KEY2 separated by ; (e.g. LT=1|Q;RT=3|E). Empty = default keys.\n"
                "RECOMP_RENDER_SCALE=%u\n"
                "RECOMP_WINDOW_SCALE=%u\n"
                "RECOMP_DISPLAY_ASPECT=%s\n"
                "RECOMP_PRESENT_FILTER=%s\n"
                "RECOMP_PRESENT_FULLSCREEN=%s\n"
                "RECOMP_FPS60=%d\n"
                "RECOMP_PB_PIPELINE=%d\n"
                "RECOMP_PRESENT_VSYNC=%d\n"
                "RECOMP_A11Y_REPEAT=%s\n"
                "RECOMP_A11Y_TOGGLE=%s\n"
                "RECOMP_HERO_OUTFIT=%s\n"
                "RECOMP_PC_TEXT_SIZE=%d\n"
                "RECOMP_MOUSE_SENS=%d\n"
                "RECOMP_MOUSE_INVERT=%d\n"
                "RECOMP_PAD_DEADZONE=%d\n"
                "RECOMP_KEYMAP=%s\n"
                "RECOMP_HORPLUS=%s\n",
                VIDEO_CONFIG_FILENAME,
                s.render_scale, s.window_scale,
                video_settings_aspect_name(s.aspect),
                video_settings_filter_name(s.filter),
                video_settings_fullscreen_token(s.borderless),
                s.fps60 ? 1 : 0, s.pipeline ? 1 : 0, s.vsync ? 1 : 0,
                a11y_repeat_token(s.a11y_repeat), a11y_toggle_token(s.a11y_toggle),
                skins_choice_token(s.outfit), video_text_size_percent(s.text_size),
                video_mouse_units(s.mouse_speed), s.mouse_invert ? 1 : 0, video_deadzone_units(s.deadzone),
                s.keymap, video_settings_horplus_token(&s)) < 0) {
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
    s->horplus = 0;
    s->filter = 0;
    s->borderless = 0;
    s->fps60 = 0;
    s->pipeline = 0;
    s->vsync = 0;
    s->a11y_repeat = A11Y_REPEAT_OFF;
    s->a11y_toggle = A11Y_TOGGLE_OFF;
    s->outfit = 0;
    s->text_size = 0;
    s->mouse_speed = VIDEO_MOUSE_DEFAULT;
    s->mouse_invert = 0;
    s->deadzone = 0;
    s->keymap[0] = 0;
}

const char *video_settings_horplus_token(const struct video_settings *s)
{
    return s && s->horplus && s->aspect > 0 && s->aspect <= 3 ? s_aspects[s->aspect] : "off";
}

void video_settings_recommended(struct video_settings *s)
{
    if (!s)
        return;
    s->render_scale = 4;
    s->aspect = 1;          /* 16:9 */
    s->horplus = 1;
    s->filter = 1;          /* linear */
    s->fps60 = 1;
    s->pipeline = 1;
    s->vsync = 1;
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
    s->horplus = s->horplus ? 1 : 0;
    if (s->filter < 0)
        s->filter = 0;
    if (s->filter > 2)
        s->filter = 2;
    if (s->borderless < DISPLAY_WINDOWED)
        s->borderless = DISPLAY_WINDOWED;
    if (s->borderless > DISPLAY_EXCLUSIVE)
        s->borderless = DISPLAY_EXCLUSIVE;
    s->fps60 = s->fps60 ? 1 : 0;
    s->pipeline = s->pipeline ? 1 : 0;
    s->vsync = s->vsync ? 1 : 0;
    if (s->a11y_repeat < 0 || s->a11y_repeat >= A11Y_REPEAT_COUNT)
        s->a11y_repeat = A11Y_REPEAT_OFF;
    if (s->a11y_toggle < 0 || s->a11y_toggle >= A11Y_TOGGLE_COUNT)
        s->a11y_toggle = A11Y_TOGGLE_OFF;
    if (s->outfit < 0 || s->outfit >= SKINS_CHOICES)
        s->outfit = 0;
    if (s->text_size < 0 || s->text_size >= VIDEO_TEXT_SIZES)
        s->text_size = 0;
    if (s->mouse_speed < 0 || s->mouse_speed >= VIDEO_MOUSE_SPEEDS)
        s->mouse_speed = VIDEO_MOUSE_DEFAULT;
    s->mouse_invert = s->mouse_invert ? 1 : 0;
    if (s->deadzone < 0 || s->deadzone >= VIDEO_DEADZONES)
        s->deadzone = 0;
    s->keymap[sizeof s->keymap - 1] = 0;
}

int video_mouse_percent(int index)
{
    static const int pct[VIDEO_MOUSE_SPEEDS] = { 50, 75, 100, 150, 200, 300 };
    return pct[index >= 0 && index < VIDEO_MOUSE_SPEEDS ? index : VIDEO_MOUSE_DEFAULT];
}

int video_mouse_units(int index)
{
    return video_mouse_percent(index) * 12;
}

int video_mouse_index(int units)
{
    int i, best = VIDEO_MOUSE_DEFAULT, bd = 1 << 30;
    if (units <= 0)
        return VIDEO_MOUSE_DEFAULT;
    for (i = 0; i < VIDEO_MOUSE_SPEEDS; i++) {
        int d = video_mouse_units(i) - units;
        if (d < 0)
            d = -d;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

int video_deadzone_percent(int index)
{
    static const int pct[VIDEO_DEADZONES] = { 0, 10, 20, 30 };
    return pct[index > 0 && index < VIDEO_DEADZONES ? index : 0];
}

int video_deadzone_units(int index)
{
    return video_deadzone_percent(index) * 32767 / 100;
}

int video_deadzone_index(int units)
{
    int i, best = 0, bd = 1 << 30;
    if (units <= 0)
        return 0;
    for (i = 0; i < VIDEO_DEADZONES; i++) {
        int d = video_deadzone_units(i) - units;
        if (d < 0)
            d = -d;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

int video_text_size_percent(int index)
{
    static const int pct[VIDEO_TEXT_SIZES] = { 100, 150, 200 };
    return pct[index > 0 && index < VIDEO_TEXT_SIZES ? index : 0];
}

int video_text_size_index(const char *s)
{
    int i, v;
    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t')
        s++;
    v = atoi(s);
    for (i = 0; i < VIDEO_TEXT_SIZES; i++)
        if (video_text_size_percent(i) == v)
            return i;
    return 0;
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
    s->horplus = c->horplus;
    if (c->present_filter >= 0 && c->present_filter <= 2)
        s->filter = c->present_filter;
    s->borderless = c->fullscreen;
    s->fps60 = c->fps60;
    s->pipeline = c->pipeline;
    s->vsync = c->vsync;
    s->a11y_repeat = c->a11y_repeat;
    s->a11y_toggle = c->a11y_toggle;
    s->outfit = c->outfit;
    s->text_size = c->text_size;
    s->mouse_speed = video_mouse_index(c->mouse_sens);
    s->mouse_invert = c->mouse_invert;
    s->deadzone = video_deadzone_index(c->deadzone);
    snprintf(s->keymap, sizeof s->keymap, "%s", c->keymap);
    video_settings_clamp(s);
}

const char *video_row_title(int row)
{
    static const char *const titles[VIDEO_ROW_COUNT] = {
        "Internal resolution",
        "Window size",
        "Aspect",
        "Wide view",
        "Present filter",
        "Window mode",
        "60 FPS gameplay",
        "Don't wait for renderer",
        "VSync",
        "Hold to repeat",
        "Hold toggle",
        "Outfit",
        "Text size",
        "Mouse speed",
        "Invert mouse",
        "Stick dead zone",
        "Keys",
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
    case VIDEO_ROW_WIDE:
        s->horplus = !s->horplus;
        return 1;
    case VIDEO_ROW_FILTER:
        before = s->filter;
        s->filter = wrap_index(s->filter, 3, dir);
        return s->filter != before;
    case VIDEO_ROW_MODE:
        before = s->borderless;
        s->borderless = wrap_index(s->borderless, 3, dir);
        return s->borderless != before;
    case VIDEO_ROW_FPS60:
        s->fps60 = !s->fps60;
        return 1;
    case VIDEO_ROW_PIPELINE:
        s->pipeline = !s->pipeline;
        return 1;
    case VIDEO_ROW_VSYNC:
        s->vsync = !s->vsync;
        return 1;
    case VIDEO_ROW_REPEAT:
        before = s->a11y_repeat;
        s->a11y_repeat = wrap_index(s->a11y_repeat, A11Y_REPEAT_COUNT, dir);
        return s->a11y_repeat != before;
    case VIDEO_ROW_TOGGLE:
        before = s->a11y_toggle;
        s->a11y_toggle = wrap_index(s->a11y_toggle, A11Y_TOGGLE_COUNT, dir);
        return s->a11y_toggle != before;
    case VIDEO_ROW_OUTFIT:
        before = s->outfit;
        s->outfit = wrap_index(s->outfit, SKINS_CHOICES, dir);
        return s->outfit != before;
    case VIDEO_ROW_TEXT:
        before = s->text_size;
        s->text_size = wrap_index(s->text_size, VIDEO_TEXT_SIZES, dir);
        return s->text_size != before;
    case VIDEO_ROW_MOUSE:
        before = s->mouse_speed;
        s->mouse_speed = wrap_index(s->mouse_speed, VIDEO_MOUSE_SPEEDS, dir);
        return s->mouse_speed != before;
    case VIDEO_ROW_INVERT:
        s->mouse_invert = !s->mouse_invert;
        return 1;
    case VIDEO_ROW_DEADZONE:
        before = s->deadzone;
        s->deadzone = wrap_index(s->deadzone, VIDEO_DEADZONES, dir);
        return s->deadzone != before;
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
    case VIDEO_ROW_WIDE:
        if (!s->horplus)
            snprintf(buf, n, "Bars");
        else if (s->aspect == 0)
            snprintf(buf, n, "Hor+ (needs a wide aspect)");
        else
            snprintf(buf, n, "Hor+");
        break;
    case VIDEO_ROW_FILTER:
        if (s->filter >= 0 && s->filter <= 2)
            snprintf(buf, n, "%s", s_filter_labels[s->filter]);
        break;
    case VIDEO_ROW_MODE:
        snprintf(buf, n, "%s", window_mode_label(s->borderless));
        break;
    case VIDEO_ROW_FPS60:
        snprintf(buf, n, "%s", s->fps60 ? "On" : "Off");
        break;
    case VIDEO_ROW_PIPELINE:
        snprintf(buf, n, "%s", s->pipeline ? "On" : "Off");
        break;
    case VIDEO_ROW_VSYNC:
        snprintf(buf, n, "%s", s->vsync ? "On" : "Off");
        break;
    case VIDEO_ROW_REPEAT:
        snprintf(buf, n, "%s", a11y_repeat_label(s->a11y_repeat));
        break;
    case VIDEO_ROW_TOGGLE:
        snprintf(buf, n, "%s", a11y_toggle_label(s->a11y_toggle));
        break;
    case VIDEO_ROW_OUTFIT:
        snprintf(buf, n, "%s", skins_choice_label(s->outfit));
        break;
    case VIDEO_ROW_TEXT:
        snprintf(buf, n, "%d%%", video_text_size_percent(s->text_size));
        break;
    case VIDEO_ROW_MOUSE:
        snprintf(buf, n, "%d%%", video_mouse_percent(s->mouse_speed));
        break;
    case VIDEO_ROW_INVERT:
        snprintf(buf, n, "%s", s->mouse_invert ? "On" : "Off");
        break;
    case VIDEO_ROW_DEADZONE:
        if (s->deadzone)
            snprintf(buf, n, "%d%%", video_deadzone_percent(s->deadzone));
        else
            snprintf(buf, n, "Off");
        break;
    case VIDEO_ROW_KEYS: {
        struct keymap_bind kb[KEYMAP_CONTROLS];
        int used = keymap_parse(s->keymap, kb, NULL);
        if (used)
            snprintf(buf, n, "%d changed", used);
        else
            snprintf(buf, n, "Default");
        break;
    }
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
        return "Window shape. At a wide shape, Wide view picks bars beside the 4:3 frame or a wider view.";
    case VIDEO_ROW_WIDE:
        return "Bars: the 4:3 picture with bars at the sides. Hor+: the game shows more at the sides "
               "(needs a wide Aspect). Takes effect the next time you start the game.";
    case VIDEO_ROW_FILTER:
        if (s && s->render_scale <= 1 && s->aspect != 0 && s->filter == 0)
            return "Nearest at internal 1x in a wide window keeps the image at the top left. "
                   "Linear or integer centres it.";
        return "Nearest, linear, or integer. Changes on the next frame.";
    case VIDEO_ROW_MODE:
        return "Windowed, borderless on the monitor, or exclusive at this window size. "
               "Wide view picks bars or a wider picture.";
    case VIDEO_ROW_FPS60:
        return "Play at 60 frames per second instead of the game's 30. Changes at once.";
    case VIDEO_ROW_PIPELINE:
        return "Faster in busy rooms: the game stops waiting for the renderer. "
               "Takes effect the next time you start the game.";
    case VIDEO_ROW_VSYNC:
        return "Wait for the display before each frame: no tearing, may add a little delay. "
               "Changes at once.";
    case VIDEO_ROW_REPEAT:
        return "Hold a button instead of tapping it fast: the game sees 10 presses a second "
               "while you hold it. Keys mapped to it work too. Changes at once.";
    case VIDEO_ROW_TOGGLE:
        return "Press a trigger once and it stays held; press it again to let go. "
               "No long holds needed. Changes at once.";
    case VIDEO_ROW_OUTFIT:
        return "Derrick's outfit, from the ones the game has. Shows the next time he appears (load a save). "
               "Story outfits still win. An outfit only shows where the game has its model loaded.";
    case VIDEO_ROW_TEXT:
        return "Text size on this PC page: 100, 150 or 200 percent. It also grows with the window. Changes at once.";
    case VIDEO_ROW_MOUSE:
        return "How fast the mouse turns the view. Changes at once.";
    case VIDEO_ROW_INVERT:
        return "On: moving the mouse up looks down. Changes at once.";
    case VIDEO_ROW_DEADZONE:
        return "Ignore small stick movements (for a worn or drifting stick). Changes at once.";
    case VIDEO_ROW_KEYS:
        return "Press Enter to choose a key for each control. Saved with Apply.";
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
