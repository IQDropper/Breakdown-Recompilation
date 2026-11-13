/*
 * qol_test.c - unit test for src/qol.c (FPS average, PNG writer, screenshot name, intro skip rule).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o qol_test src/qol_test.c src/qol.c
 * Optional argument: a path to write a small test PNG to (to open in an image viewer).
 */
#include "qol.h"

#include <math.h>
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

static int near(double a, double b, double eps)
{
    return fabs(a - b) <= eps;
}

/* ---- FPS ---- */

static void test_fps(void)
{
    static struct qol_fps f;
    struct qol_fps_stats s;
    char buf[64];
    uint64_t t;
    int i, have;

    qol_fps_init(&f);
    expect("fps: empty has nothing", !qol_fps_stats(&f, 0, &s));
    qol_fps_format(&s, 0, buf, sizeof buf);
    expect("fps: empty text", !strcmp(buf, "-- FPS"));
    qol_fps_frame(&f, 1000);
    expect("fps: one frame has nothing", !qol_fps_stats(&f, 1000, &s));

    /* steady 60 FPS for 3 seconds: 16667 us apart */
    qol_fps_init(&f);
    for (i = 0, t = 5000000; i <= 180; i++, t += 16667)
        qol_fps_frame(&f, t);
    t -= 16667;
    have = qol_fps_stats(&f, t, &s);
    expect("fps: steady 60 has stats", have);
    expect("fps: steady 60 ~ 60.0", near(s.fps, 60.0, 0.05));
    expect("fps: steady 60 ~ 16.7 ms", near(s.ms_avg, 16.667, 0.01));
    expect("fps: steady 60 max = 16.7 ms", near(s.ms_max, 16.667, 0.01));
    expect("fps: steady 60 counts ~ one second of frames", s.frames >= 59 && s.frames <= 61);
    expect("fps: old frames dropped", f.count <= 62);
    qol_fps_format(&s, have, buf, sizeof buf);
    expect("fps: text", !strcmp(buf, "60.0 FPS  16.7 ms  max 16.7"));

    /* 30 FPS with one 100 ms hitch inside the last second */
    qol_fps_init(&f);
    for (i = 0, t = 0; i < 60; i++, t += 33333)
        qol_fps_frame(&f, t);
    t += 100000 - 33333;
    qol_fps_frame(&f, t);
    for (i = 0; i < 10; i++) {
        t += 33333;
        qol_fps_frame(&f, t);
    }
    have = qol_fps_stats(&f, t, &s);
    expect("fps: hitch max = 100 ms", have && near(s.ms_max, 100.0, 0.01));
    expect("fps: hitch lowers the average", s.fps < 30.0 && s.fps > 25.0);

    /* the game stops (a long load): after a second with no frame there is nothing to show */
    expect("fps: stalled 0.5 s still shows", qol_fps_stats(&f, t + 500000, &s));
    expect("fps: stalled 1 s shows nothing", !qol_fps_stats(&f, t + 1000000, &s));

    /* time going back is ignored */
    qol_fps_init(&f);
    qol_fps_frame(&f, 2000000);
    qol_fps_frame(&f, 1000000);
    expect("fps: older time ignored", f.count == 1);

    /* more frames than the ring holds (2000 FPS): stays valid, keeps the newest */
    qol_fps_init(&f);
    for (i = 0, t = 0; i < 3000; i++, t += 500)
        qol_fps_frame(&f, t);
    t -= 500;
    have = qol_fps_stats(&f, t, &s);
    expect("fps: full ring count", f.count == QOL_FPS_MAX);
    expect("fps: full ring 2000 FPS", have && near(s.fps, 2000.0, 1.0));
}

/* ---- PNG ---- */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* Reads back what qol_png_encode wrote: chunks + CRCs, stored zlib blocks + Adler-32, pixels. */
static int check_png(const uint8_t *png, size_t n, const uint8_t *bgra, int w, int h, int stride)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    size_t o = 8, rawn = (size_t)h * (1 + (size_t)w * 3), got = 0;
    uint8_t *raw = malloc(rawn + 1);
    int seen_ihdr = 0, seen_iend = 0, ok = 1, x, y;
    if (!raw || n < 8 || memcmp(png, sig, 8))
        ok = 0;
    while (ok && o + 12 <= n) {
        uint32_t len = rd32(png + o);
        const uint8_t *type = png + o + 4, *d = png + o + 8;
        if (o + 12 + len > n || qol_crc32(type, len + 4) != rd32(d + len)) {
            ok = 0;
            break;
        }
        if (!memcmp(type, "IHDR", 4)) {
            ok = len == 13 && (int)rd32(d) == w && (int)rd32(d + 4) == h && d[8] == 8 && d[9] == 2 && !d[10]
                 && !d[11] && !d[12];
            seen_ihdr = 1;
        } else if (!memcmp(type, "IDAT", 4)) {
            size_t p = 2;
            int last = 0;
            if (len < 6 || d[0] != 0x78 || ((d[0] << 8) | d[1]) % 31) {
                ok = 0;
                break;
            }
            while (ok && !last) {
                unsigned bl, nl;
                if (p + 5 > len) {
                    ok = 0;
                    break;
                }
                last = d[p] & 1;
                if (d[p] & 6)
                    ok = 0;   /* not a stored block */
                bl = d[p + 1] | d[p + 2] << 8;
                nl = d[p + 3] | d[p + 4] << 8;
                if ((bl ^ 0xFFFFu) != nl || got + bl > rawn || p + 5 + bl > len)
                    ok = 0;
                if (ok)
                    memcpy(raw + got, d + p + 5, bl);
                got += bl;
                p += 5 + bl;
            }
            if (ok && (p + 4 != len || got != rawn || qol_adler32(raw, rawn) != rd32(d + p)))
                ok = 0;
        } else if (!memcmp(type, "IEND", 4)) {
            seen_iend = len == 0 && o + 12 == n;
        }
        o += 12 + len;
    }
    for (y = 0; ok && y < h; y++) {
        const uint8_t *row = raw + (size_t)y * (1 + (size_t)w * 3);
        if (row[0] != 0)
            ok = 0;
        for (x = 0; ok && x < w; x++) {
            const uint8_t *px = bgra + (size_t)y * stride + (size_t)x * 4;
            if (row[1 + x * 3] != px[2] || row[2 + x * 3] != px[1] || row[3 + x * 3] != px[0])
                ok = 0;
        }
    }
    free(raw);
    return ok && seen_ihdr && seen_iend;
}

static uint8_t *make_picture(int w, int h, int stride)
{
    uint8_t *p = calloc((size_t)stride * h, 1);
    int x, y;
    for (y = 0; p && y < h; y++)
        for (x = 0; x < w; x++) {
            uint8_t *px = p + (size_t)y * stride + (size_t)x * 4;
            px[0] = (uint8_t)(x * 7);         /* B */
            px[1] = (uint8_t)(y * 3);         /* G */
            px[2] = (uint8_t)(x ^ y);         /* R */
            px[3] = 0xEE;                     /* A (dropped) */
        }
    return p;
}

static void test_png(const char *write_to)
{
    static const uint8_t iend[4] = { 'I', 'E', 'N', 'D' };
    static const char wiki[] = "Wikipedia";
    static const char fox[] = "The quick brown fox jumps over the lazy dog";
    int sizes[][2] = { { 1, 1 }, { 3, 2 }, { 200, 120 }, { 640, 480 } }, i;
    expect("crc32: IEND", qol_crc32(iend, 4) == 0xAE426082u);
    expect("crc32: fox", qol_crc32((const uint8_t *)fox, strlen(fox)) == 0x414FA339u);
    expect("adler32: Wikipedia", qol_adler32((const uint8_t *)wiki, strlen(wiki)) == 0x11E60398u);
    expect("adler32: empty", qol_adler32(NULL, 0) == 1);
    expect("png size: empty is 0", qol_png_size(0, 10) == 0 && qol_png_size(10, 0) == 0);
    expect("png size: 1x1", qol_png_size(1, 1) == 8 + 25 + 12 + (2 + 5 + 4 + 4) + 12);
    for (i = 0; i < 4; i++) {
        int w = sizes[i][0], h = sizes[i][1], stride = w * 4 + 8;   /* padded rows */
        uint8_t *pic = make_picture(w, h, stride);
        size_t cap = qol_png_size(w, h), n;
        uint8_t *png = malloc(cap);
        char name[64];
        n = qol_png_encode(pic, w, h, stride, png, cap);
        snprintf(name, sizeof name, "png %dx%d: size", w, h);
        expect(name, n == cap);
        snprintf(name, sizeof name, "png %dx%d: reads back", w, h);
        expect(name, check_png(png, n, pic, w, h, stride));
        snprintf(name, sizeof name, "png %dx%d: too small buffer refused", w, h);
        expect(name, qol_png_encode(pic, w, h, stride, png, cap - 1) == 0);
        if (write_to && i == 2) {
            FILE *f = fopen(write_to, "wb");
            if (f) {
                fwrite(png, 1, n, f);
                fclose(f);
                printf("wrote %s\n", write_to);
            }
        }
        free(png);
        free(pic);
    }
}

/* ---- name ---- */

static void test_name(void)
{
    char b[64];
    qol_shot_name(2026, 11, 11, 9, 5, 7, 0, b, sizeof b);
    expect("name: plain", !strcmp(b, "breakdown_2026-11-11_09-05-07.png"));
    qol_shot_name(2026, 1, 2, 23, 59, 59, 1, b, sizeof b);
    expect("name: seq 1 is plain", !strcmp(b, "breakdown_2026-01-02_23-59-59.png"));
    qol_shot_name(2026, 1, 2, 23, 59, 59, 3, b, sizeof b);
    expect("name: seq 3", !strcmp(b, "breakdown_2026-01-02_23-59-59_3.png"));
}

/* ---- intro skip ---- */

static void test_intro(void)
{
    expect("intro: no movie", qol_intro_decide(NULL, 1, 0, 3.0f, 0) == QOL_INTRO_NONE);
    expect("intro: logo is not the intro", qol_intro_decide("namcologo", 1, 2, 3.0f, 0) == QOL_INTRO_NONE);
    expect("intro: the Dolby logo is skipped too", qol_intro_decide("dolby", 1, 0, 3.0f, 0) == QOL_INTRO_SKIP);
    expect("intro: Dolby waits 0.5 s too", qol_intro_decide("dolby", 1, 0, 0.3f, 0) == QOL_INTRO_WAIT);
    expect("intro: other names that start with dolby are not", qol_intro_decide("dolbyx", 1, 0, 3.0f, 0) == QOL_INTRO_NONE);
    expect("intro: credits are not the intro", qol_intro_decide("opening_credit", 1, 0, 3.0f, 0) == QOL_INTRO_NONE);
    expect("intro: not playing", qol_intro_decide("opening_b", 0, 0, 3.0f, 0) == QOL_INTRO_NONE);
    expect("intro: too early", qol_intro_decide("opening_b", 1, 0, 0.2f, 0) == QOL_INTRO_WAIT);
    expect("intro: exactly 0.5 s waits", qol_intro_decide("opening_b", 1, 0, 0.5f, 0) == QOL_INTRO_WAIT);
    expect("intro: NaN time waits", qol_intro_decide("opening_b", 1, 0, nanf(""), 0) == QOL_INTRO_WAIT);
    expect("intro: skip", qol_intro_decide("opening_b", 1, 0, 0.6f, 0) == QOL_INTRO_SKIP);
    expect("intro: 5.1 version", qol_intro_decide("opening_b51", 3, 0, 1.0f, 0) == QOL_INTRO_SKIP);
    expect("intro: other region version", qol_intro_decide("opening_bt", 1, 0, 1.0f, 0) == QOL_INTRO_SKIP);
    expect("intro: already fading", qol_intro_decide("opening_b", 1, 0, 9.0f, 1) == QOL_INTRO_LEAVE);
    expect("intro: unskippable mode", qol_intro_decide("opening_b", 1, 2, 9.0f, 0) == QOL_INTRO_LEAVE);
}

int main(int argc, char **argv)
{
    test_fps();
    test_png(argc > 1 ? argv[1] : NULL);
    test_name();
    test_intro();
    if (g_fail) {
        fprintf(stderr, "%d qol test(s) FAILED\n", g_fail);
        return 1;
    }
    printf("all qol tests passed\n");
    return 0;
}
