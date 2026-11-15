/*
 * speedrun_theme_test.c - unit test for speedrun_theme.ini reading (src/speedrun_theme.c).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o speedrun_theme_test src/speedrun_theme_test.c src/speedrun_theme.c
 */
#include "speedrun_theme.h"

#include <stdio.h>
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

static const char *game_name(const char *area)
{
    return strcmp(area, "hon_under") == 0 ? "7F / Main Research Center" : NULL;
}

int main(void)
{
    struct sr_theme t;
    char out[128];
    int bad;

    sr_theme_default(&t);
    expect("default: Consolas, top right, box, both lines", !strcmp(t.font, "Consolas") && t.corner == SR_TOP_RIGHT
                                                               && t.box && t.show_real && t.show_split && !t.title[0]);
    expect("default: old colors", t.color_running == 0x78E678 && t.color_stopped == 0xB4B4B4 && t.color_box == 0x0C0C10);

    bad = sr_theme_parse(&t,
                         "\xEF\xBB\xBF# my theme\r\n"
                         "[timer]\r\n"
                         "title = \"Any% # glitchless\"   # quoted keeps the #\r\n"
                         "font = Segoe UI\r\n"
                         "SIZE = 150%\r\n"
                         "bold = no\r\n"
                         "color_running = #FF8800\r\n"
                         "color_stopped = 10, 20, 30\r\n"
                         "color_box = #000000 ; black\r\n"
                         "box = off\r\n"
                         "opacity = 5\r\n"
                         "corner = bottom left\r\n"
                         "show_real_time = no\r\n"
                         "\r\n"
                         "[splits]\r\n"
                         "hon_under = Floor 7\r\n"
                         "hon_under_B = Floor 7, part 2\r\n"
                         "bettou_A = Annex \xE2\x98\x85\r\n");
    expect("parse: no bad lines", bad == 0);
    expect("parse: title, quotes and # kept", !strcmp(t.title, "Any% # glitchless"));
    expect("parse: font with a space", !strcmp(t.font, "Segoe UI"));
    expect("parse: size 150%, key any case", t.size == 150);
    expect("parse: bold off", t.bold == 0);
    expect("parse: #RRGGBB", t.color_running == 0xFF8800);
    expect("parse: r, g, b", t.color_stopped == 0x0A141E);
    expect("parse: # color then ; comment", t.color_box == 0);
    expect("parse: box off", t.box == 0);
    expect("parse: opacity clamped to 10", t.opacity == 10);
    expect("parse: corner with a space", t.corner == SR_BOTTOM_LEFT);
    expect("parse: real-time line off, split line still on", !t.show_real && t.show_split);
    expect("parse: three names", t.nnames == 3);

    sr_theme_split_name(&t, "hon_under_B", game_name, out, sizeof out);
    expect("name: section name wins", !strcmp(out, "Floor 7, part 2"));
    sr_theme_split_name(&t, "hon_under_A", game_name, out, sizeof out);
    expect("name: whole-area name", !strcmp(out, "Floor 7"));
    sr_theme_split_name(&t, "bettou_A", game_name, out, sizeof out);
    expect("name: UTF-8 kept", !strcmp(out, "Annex \xE2\x98\x85"));
    sr_theme_split_name(&t, "gesui_A", game_name, out, sizeof out);
    expect("name: unknown stays as the file name", !strcmp(out, "gesui_A"));
    sr_theme_default(&t);
    sr_theme_split_name(&t, "hon_under_C", game_name, out, sizeof out);
    expect("name: no theme -> the game's area name", !strcmp(out, "7F / Main Research Center"));
    sr_theme_split_name(NULL, "st01_A", NULL, out, sizeof out);
    expect("name: no theme, no fallback", !strcmp(out, "st01_A"));

    sr_theme_default(&t);
    bad = sr_theme_parse(&t, "size = big\ncolour = red\ncorner = middle\nnot a setting\n[other]\nfont =\n");
    expect("bad: six lines counted", bad == 6);
    expect("bad: defaults kept", t.size == 100 && t.corner == SR_TOP_RIGHT && !strcmp(t.font, "Consolas"));

    sr_theme_default(&t);
    sr_theme_parse(&t, "size = 999\nsize = 10\n");
    expect("size clamped to 50", t.size == 50);
    sr_theme_parse(&t, "[splits]\nhon_under = A\nhon_under = B\n");
    expect("names: a repeat replaces", t.nnames == 1 && !strcmp(t.names[0].name, "B"));

    /* the shipped example (run from the repo root) must read cleanly and give the default look */
    {
        FILE *f = fopen("speedrun_theme.example.ini", "rb");
        static char text[16384];
        struct sr_theme d;
        size_t len;
        if (f) {
            len = fread(text, 1, sizeof text - 1, f);
            fclose(f);
            text[len] = 0;
            sr_theme_default(&t);
            sr_theme_default(&d);
            bad = sr_theme_parse(&t, text);
            expect("example: every line understood", bad == 0);
            expect("example: same as the default look", !memcmp(&t, &d, sizeof t));
        } else {
            printf("skip example (run from the repo root)\n");
        }
    }

    printf(g_fail ?"%d FAILED\n" : "all passed\n", g_fail);
    return g_fail != 0;
}
