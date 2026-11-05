/*
 * video_config_test.c - config file and the Video menu's value model.
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -I src -o video_config_test \
 *       src/video_config_test.c src/video_config.c src/display_host.c
 */
#include "display_host.h"
#include "video_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *env_empty(const char *name)
{
    (void)name;
    return NULL;
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (fputs(text, f) < 0) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static void test_join(void)
{
    char buf[64];

    expect("join unix",
           video_config_join("/tmp/x", buf, sizeof buf) == 0
           && strcmp(buf, "/tmp/x/breakdown_video.cfg") == 0);
    expect("join slash",
           video_config_join("/tmp/x/", buf, sizeof buf) == 0
           && strcmp(buf, "/tmp/x/breakdown_video.cfg") == 0);
    expect("join windows",
           video_config_join("C:\\games", buf, sizeof buf) == 0
           && strcmp(buf, "C:\\games\\breakdown_video.cfg") == 0);
    expect("join windows slash",
           video_config_join("C:\\games\\", buf, sizeof buf) == 0
           && strcmp(buf, "C:\\games\\breakdown_video.cfg") == 0);
    expect("join tiny fails", video_config_join("/tmp/x", buf, 8) == -1);
}

static void test_roundtrip(void)
{
    struct video_settings s, loaded;
    struct display_choice c;
    const char *path = "video_config_roundtrip.cfg";
    char aspect[8];

    video_settings_stock(&s);
    s.render_scale = 2;
    s.window_scale = 3;
    s.aspect = video_settings_aspect_index("21:9");
    s.filter = 2;
    s.borderless = 1;
    expect("save", video_config_save(path, &s) == 0);
    video_config_reset();
    expect("saved cleared", video_config_getenv("RECOMP_RENDER_SCALE") == NULL);
    expect("load", video_config_load(path) == 1);
    expect("loaded scale raw",
           video_config_getenv("RECOMP_RENDER_SCALE")
           && strcmp(video_config_getenv("RECOMP_RENDER_SCALE"), "2") == 0);
    expect("loaded aspect",
           strcmp(video_config_getenv("RECOMP_DISPLAY_ASPECT"), "21:9") == 0);
    expect("loaded filter",
           strcmp(video_config_getenv("RECOMP_PRESENT_FILTER"), "integer") == 0);
    expect("loaded fullscreen",
           strcmp(video_config_getenv("RECOMP_PRESENT_FULLSCREEN"), "1") == 0);
    display_host_set_sources(env_empty, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    video_settings_from_choice(&c, &loaded);
    expect("roundtrip scale", loaded.render_scale == 2 && loaded.window_scale == 3);
    expect("roundtrip aspect", loaded.aspect == s.aspect && loaded.filter == 2);
    expect("roundtrip borderless", loaded.borderless == 1);
    expect("roundtrip size", c.window_w == 3360 && c.window_h == 1440);
    snprintf(aspect, sizeof aspect, "%s", video_settings_aspect_name(loaded.aspect));
    expect("aspect name", strcmp(aspect, "21:9") == 0);
    remove(path);
}

static void test_parse(void)
{
    struct display_choice c;
    const char *path = "video_config_parse.cfg";
    const char *text =
        "# comment\n"
        "\n"
        "NOT_A_KEY=1\n"
        "  RECOMP_DISPLAY_ASPECT = 16:10  \n"
        "RECOMP_RENDER_SCALE=9\n"
        "RECOMP_RENDER_SCALE=2\n"
        "RECOMP_WINDOW_SCALE=nope\n"
        "; also a comment\n"
        "RECOMP_PRESENT_FILTER=linear\n";

    expect("write parse", write_text(path, text) == 0);
    expect("missing is empty", video_config_load("video_config_missing.cfg") == 0);
    expect("missing leaves nothing", video_config_getenv("RECOMP_RENDER_SCALE") == NULL);
    expect("load parse", video_config_load(path) == 1);
    expect("unknown key ignored", video_config_getenv("NOT_A_KEY") == NULL);
    expect("trim and last key",
           strcmp(video_config_getenv("RECOMP_RENDER_SCALE"), "2") == 0);
    expect("trimmed aspect",
           strcmp(video_config_getenv("RECOMP_DISPLAY_ASPECT"), "16:10") == 0);
    expect("filter kept",
           strcmp(video_config_getenv("RECOMP_PRESENT_FILTER"), "linear") == 0);
    expect("absent fullscreen", video_config_getenv("RECOMP_PRESENT_FULLSCREEN") == NULL);
    display_host_set_sources(env_empty, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    expect("clamped bad window", c.window_clamped && c.window_w == 768 && c.window_h == 480);
    expect("parsed aspect", strcmp(c.aspect, "16:10") == 0 && c.render_scale == 2);
    expect("parsed filter", c.present_filter == 1 && c.fullscreen == 0);
    remove(path);
}

/* Env unset: file applies. Env set: env wins, including an explicit 0. */
static char *s_env_key;
static char *s_env_val;

static char *env_one(const char *name)
{
    if (s_env_key && strcmp(name, s_env_key) == 0)
        return s_env_val;
    return NULL;
}

static void test_precedence(void)
{
    struct display_choice c;
    const char *path = "video_config_prec.cfg";

    expect("write prec",
           write_text(path,
                      "RECOMP_RENDER_SCALE=3\n"
                      "RECOMP_WINDOW_SCALE=2\n"
                      "RECOMP_DISPLAY_ASPECT=16:9\n"
                      "RECOMP_PRESENT_FILTER=nearest\n"
                      "RECOMP_PRESENT_FULLSCREEN=1\n") == 0);
    expect("load prec", video_config_load(path) == 1);

    display_host_set_sources(env_empty, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    expect("file applies",
           c.render_scale == 3 && c.fullscreen == 1 && strcmp(c.aspect, "16:9") == 0
           && c.window_w == 1707 && c.window_h == 960 && c.present_filter == 0
           && c.filter_promoted == 0);

    s_env_key = "RECOMP_RENDER_SCALE";
    s_env_val = "1";
    display_host_set_sources(env_one, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    expect("env scale wins", c.render_scale == 1 && c.fullscreen == 1);

    s_env_key = "RECOMP_PRESENT_FULLSCREEN";
    s_env_val = "0";
    display_host_choose(display_getenv_layered, &c);
    expect("env fullscreen 0 wins", c.fullscreen == 0 && c.render_scale == 3);

    s_env_key = "RECOMP_RENDER_SCALE";
    s_env_val = "";
    display_host_choose(display_getenv_layered, &c);
    expect("empty env uses file", c.render_scale == 3);

    video_config_reset();
    display_host_set_sources(env_empty, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    expect("neither is stock",
           c.render_scale == 1 && c.window_w == 640 && c.window_h == 480
           && strcmp(c.aspect, "4:3") == 0 && c.present_filter == 0 && c.fullscreen == 0);
    remove(path);
}

static void test_promote_from_partial_file(void)
{
    struct display_choice c;
    const char *path = "video_config_wide.cfg";

    expect("write wide",
           write_text(path, "RECOMP_DISPLAY_ASPECT=16:9\n") == 0);
    expect("load wide", video_config_load(path) == 1);
    display_host_set_sources(env_empty, video_config_getenv);
    display_host_choose(display_getenv_layered, &c);
    expect("wide scale 1 promotes linear",
           c.filter_promoted == 1 && c.present_filter == 1 && c.render_scale == 1
           && c.window_w == 853 && c.window_h == 480);
    remove(path);
}

static void test_menu_model(void)
{
    struct video_settings s;
    struct display_choice c;
    char buf[32];

    video_settings_stock(&s);
    expect("stock values",
           s.render_scale == 1 && s.window_scale == 1 && s.aspect == 0
           && s.filter == 0 && s.borderless == 0);
    expect("titles",
           strcmp(video_row_title(VIDEO_ROW_SCALE), "Internal resolution") == 0
           && strcmp(video_row_title(VIDEO_ROW_WINDOW), "Window size") == 0
           && strcmp(video_row_title(VIDEO_ROW_ASPECT), "Aspect") == 0
           && strcmp(video_row_title(VIDEO_ROW_FILTER), "Present filter") == 0
           && strcmp(video_row_title(VIDEO_ROW_MODE), "Window mode") == 0
           && strcmp(video_row_title(VIDEO_ROW_APPLY), "Apply") == 0
           && strcmp(video_row_title(VIDEO_ROW_OK), "OK") == 0
           && strcmp(video_row_title(VIDEO_ROW_CANCEL), "Cancel") == 0);
    expect("value labels",
           strcmp(video_settings_value(&s, VIDEO_ROW_SCALE, buf, sizeof buf), "1x") == 0
           && strcmp(video_settings_value(&s, VIDEO_ROW_MODE, buf, sizeof buf), "Windowed") == 0
           && strcmp(video_settings_value(&s, VIDEO_ROW_FILTER, buf, sizeof buf), "Nearest") == 0
           && strcmp(video_settings_value(&s, VIDEO_ROW_ASPECT, buf, sizeof buf), "4:3") == 0);
    expect("scale detail", strstr(video_row_detail(&s, VIDEO_ROW_SCALE), "next frame") != NULL);
    expect("window detail", strstr(video_row_detail(&s, VIDEO_ROW_WINDOW), "Resizes the window now") != NULL);
    expect("aspect detail",
           strstr(video_row_detail(&s, VIDEO_ROW_ASPECT), "Letterbox") != NULL
           && strstr(video_row_detail(&s, VIDEO_ROW_ASPECT), "Hor+") != NULL);
    expect("mode detail",
           strstr(video_row_detail(&s, VIDEO_ROW_MODE), "Exclusive fullscreen") != NULL
           && strstr(video_row_detail(&s, VIDEO_ROW_MODE), "not available") != NULL);
    expect("needs window",
           video_row_needs_window(VIDEO_ROW_SCALE) == 0
           && video_row_needs_window(VIDEO_ROW_FILTER) == 0
           && video_row_needs_window(VIDEO_ROW_WINDOW) == 1
           && video_row_needs_window(VIDEO_ROW_ASPECT) == 1
           && video_row_needs_window(VIDEO_ROW_MODE) == 1
           && video_row_needs_window(VIDEO_ROW_APPLY) == 0);

    expect("nudge scale stops",
           video_settings_nudge(&s, VIDEO_ROW_SCALE, -1) == 0 && s.render_scale == 1);
    expect("nudge scale up",
           video_settings_nudge(&s, VIDEO_ROW_SCALE, 1) == 1 && s.render_scale == 2);
    s.render_scale = 4;
    expect("nudge scale cap",
           video_settings_nudge(&s, VIDEO_ROW_SCALE, 1) == 0 && s.render_scale == 4);
    s.window_scale = 6;
    expect("nudge window cap",
           video_settings_nudge(&s, VIDEO_ROW_WINDOW, 1) == 0 && s.window_scale == 6);
    s.aspect = video_settings_aspect_index("21:9");
    expect("aspect wraps",
           video_settings_nudge(&s, VIDEO_ROW_ASPECT, 1) == 1
           && strcmp(video_settings_aspect_name(s.aspect), "4:3") == 0);
    s.aspect = 0;
    expect("aspect wraps back",
           video_settings_nudge(&s, VIDEO_ROW_ASPECT, -1) == 1
           && strcmp(video_settings_aspect_name(s.aspect), "21:9") == 0);
    s.filter = 2;
    expect("filter wraps",
           video_settings_nudge(&s, VIDEO_ROW_FILTER, 1) == 1
           && strcmp(video_settings_filter_name(s.filter), "nearest") == 0);
    expect("filter label",
           strcmp(video_settings_value(&s, VIDEO_ROW_FILTER, buf, sizeof buf), "Nearest") == 0);
    expect("mode toggles",
           video_settings_nudge(&s, VIDEO_ROW_MODE, 1) == 1 && s.borderless == 1
           && strcmp(video_settings_value(&s, VIDEO_ROW_MODE, buf, sizeof buf), "Borderless") == 0);
    expect("action nudge ignored",
           video_settings_nudge(&s, VIDEO_ROW_OK, 1) == 0 && s.borderless == 1);

    s.render_scale = 1;
    s.aspect = video_settings_aspect_index("16:9");
    s.filter = 0;
    s.borderless = 0;
    expect("top-left warning",
           strstr(video_row_detail(&s, VIDEO_ROW_FILTER), "top left") != NULL);
    s.filter = 1;
    expect("linear has no top-left warning",
           strstr(video_row_detail(&s, VIDEO_ROW_FILTER), "top left") == NULL);
    s.render_scale = 2;
    s.filter = 0;
    expect("scale 2 nearest has no top-left warning",
           strstr(video_row_detail(&s, VIDEO_ROW_FILTER), "top left") == NULL);

    memset(&c, 0, sizeof c);
    c.render_scale = 3;
    c.window_w = 1536;
    c.window_h = 960;
    c.present_filter = 1;
    c.fullscreen = 0;
    snprintf(c.aspect, sizeof c.aspect, "16:10");
    video_settings_from_choice(&c, &s);
    expect("from choice",
           s.render_scale == 3 && s.window_scale == 2
           && strcmp(video_settings_aspect_name(s.aspect), "16:10") == 0
           && s.filter == 1 && s.borderless == 0);

    expect("bad aspect index", video_settings_aspect_index("hor+") < 0);
    expect("save null fails", video_config_save(NULL, &s) == -1);
}

int main(void)
{
    test_join();
    test_roundtrip();
    test_parse();
    test_precedence();
    test_promote_from_partial_file();
    test_menu_model();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    puts("all passed");
    return 0;
}
