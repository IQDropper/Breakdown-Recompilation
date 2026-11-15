/* speedrun_theme.h - the player's own look for the speedrun timer, read from speedrun_theme.ini.
 * No Windows here: the host (src/speedrun_host.c) reads the file and draws what the theme says.
 *
 * The file is plain "key = value" lines; '#' or ';' starts a comment. Unknown keys are ignored, a bad value
 * keeps the default. A [splits] section renames areas: "hon_under = Floor 7" (the whole area) or
 * "hon_under_B = Floor 7, part 2" (one section). speedrun_theme.example.ini lists every key. */
#ifndef SPEEDRUN_THEME_H
#define SPEEDRUN_THEME_H

#include <stddef.h>
#include <stdint.h>

#define SR_THEME_TEXT_LEN  96
#define SR_THEME_FONT_LEN  64
#define SR_THEME_MAX_NAMES 64

enum sr_corner { SR_TOP_RIGHT, SR_TOP_LEFT, SR_BOTTOM_RIGHT, SR_BOTTOM_LEFT };

struct sr_theme_name {
    char id[48];                  /* "hon_under" or "hon_under_B" */
    char name[SR_THEME_TEXT_LEN]; /* what the timer shows (UTF-8) */
};

struct sr_theme {
    char title[SR_THEME_TEXT_LEN];   /* "" = no title line (UTF-8) */
    char font[SR_THEME_FONT_LEN];    /* any installed font; default "Consolas" */
    int size;                        /* percent of the default size, 50..300 */
    int bold;                        /* the big time in bold */
    uint32_t color_running;          /* 0xRRGGBB: the time while it counts */
    uint32_t color_stopped;          /* the time while ready, loading or paused */
    uint32_t color_text;             /* the split and real-time lines */
    uint32_t color_title;            /* the title line */
    uint32_t color_box;              /* the box behind the text */
    int box;                         /* 0 = no box, only the text shows */
    int opacity;                     /* percent, 10..100, the whole timer */
    enum sr_corner corner;
    int show_real;                   /* the "real <time>" line */
    int show_split;                  /* the "#n <area>" line */
    int nnames;
    struct sr_theme_name names[SR_THEME_MAX_NAMES];
};

/* The look the timer had before themes: Consolas, green/grey time, dark box, top right, no title. */
void sr_theme_default(struct sr_theme *t);

/* Reads the file's text over the defaults already in t. Returns how many lines were not understood
 * (0 = all fine); the host logs that count so a typo is not silent. */
int sr_theme_parse(struct sr_theme *t, const char *text);

/* The name to show for a split area ("hon_under_A"): the player's own name for that section, else for the
 * whole area ("hon_under"), else the game's area name (fallback(area), may be NULL), else the area as is.
 * Writes UTF-8 into out. */
void sr_theme_split_name(const struct sr_theme *t, const char *split_area, const char *(*fallback)(const char *area),
                         char *out, size_t n);

#endif /* SPEEDRUN_THEME_H */
