/* qol.h - small quality-of-life helpers (S5.3). No Windows, no game memory: unit tested in src/qol_test.c.
 *   - FPS / frame time over the last second (the overlay in src/qol_host.c, RECOMP_FPS_OVERLAY=1, F5)
 *   - a PNG writer with no library (stored deflate blocks), for F12 screenshots
 *   - the screenshot file name
 *   - the intro movie skip rule (RECOMP_SKIP_INTRO=1, used by src/recomp_manual.c) */
#ifndef QOL_H
#define QOL_H

#include <stddef.h>
#include <stdint.h>

/* ---- FPS ---- */

#define QOL_FPS_WINDOW_US 1000000u   /* average over the last second */
#define QOL_FPS_MAX       1024       /* frame times kept (enough for 1000 FPS) */

struct qol_fps {
    uint64_t t[QOL_FPS_MAX];   /* frame end times, microseconds, oldest first from head */
    int head, count;
};

struct qol_fps_stats {
    int frames;        /* frame intervals counted */
    double fps;        /* average frames per second */
    double ms_avg;     /* average frame time */
    double ms_max;     /* slowest frame in the window */
};

void qol_fps_init(struct qol_fps *f);
/* One frame ended at t (us). Times never go back; an older t is ignored. */
void qol_fps_frame(struct qol_fps *f, uint64_t t);
/* Stats over the frames that ended in (now - window, now]. 0 when fewer than 2 frames are in it. */
int qol_fps_stats(const struct qol_fps *f, uint64_t now, struct qol_fps_stats *out);
/* "60.0 FPS  16.7 ms  max 33.4" or "-- FPS" when there is nothing to show. */
void qol_fps_format(const struct qol_fps_stats *s, int have, char *out, size_t n);

/* ---- PNG ---- */

uint32_t qol_crc32(const uint8_t *p, size_t n);
uint32_t qol_adler32(const uint8_t *p, size_t n);
/* Bytes qol_png_encode writes for a w x h picture (0 if too big or empty). */
size_t qol_png_size(int w, int h);
/* bgra: top-down rows of w BGRA pixels (GDI 32-bit DIB), stride bytes apart. Writes an 8-bit RGB PNG.
 * Returns the bytes written, 0 if cap is too small. */
size_t qol_png_encode(const uint8_t *bgra, int w, int h, int stride, uint8_t *out, size_t cap);

/* ---- screenshot name ---- */

/* "breakdown_2026-11-11_14-05-09.png"; seq > 1 adds "_<seq>" before ".png" (two shots in one second). */
void qol_shot_name(int year, int mon, int day, int hour, int min, int sec, int seq, char *out, size_t n);

/* ---- intro skip ----
 * The movie player object is [0x29C61C]. Fields used (from the movie update 0x11F70 and the game's own
 * "button pressed: fade out" path 0x11880):
 *   +0x34 bit 0 = playing; +0x98 = movie name ("opening_b", "opening_b51", "opening_bt", ...);
 *   +0xC8 = seconds played (float); +0xCC = play mode, bit 1 = cannot be skipped (logos use mode 2);
 *   +0xE4 = fading out (the skip already started). */
enum qol_intro {
    QOL_INTRO_NONE,    /* no intro movie playing: keep watching */
    QOL_INTRO_WAIT,    /* the intro is playing, under 0.5 s in (the game's own skip waits for that too) */
    QOL_INTRO_SKIP,    /* start the fade-out now, the way a button press does */
    QOL_INTRO_LEAVE,   /* the intro is already fading, or marked unskippable: leave it and stop watching */
};
enum qol_intro qol_intro_decide(const char *name, uint32_t flags34, uint32_t mode_cc, float played_s,
                                uint32_t fading_e4);

#endif
