/* qol.c - pure helpers for the S5.3 quality-of-life features. See qol.h. */
#include "qol.h"

#include <stdio.h>
#include <string.h>

/* ---- FPS ---- */

void qol_fps_init(struct qol_fps *f)
{
    memset(f, 0, sizeof *f);
}

static uint64_t fps_at(const struct qol_fps *f, int i)
{
    return f->t[(f->head + i) % QOL_FPS_MAX];
}

void qol_fps_frame(struct qol_fps *f, uint64_t t)
{
    if (f->count > 0 && t < fps_at(f, f->count - 1))
        return;
    /* drop what is older than the window (keep one before it: it starts the first interval) */
    while (f->count > 1 && fps_at(f, 1) + QOL_FPS_WINDOW_US <= t) {
        f->head = (f->head + 1) % QOL_FPS_MAX;
        f->count--;
    }
    if (f->count == QOL_FPS_MAX) {
        f->head = (f->head + 1) % QOL_FPS_MAX;
        f->count--;
    }
    f->t[(f->head + f->count) % QOL_FPS_MAX] = t;
    f->count++;
}

int qol_fps_stats(const struct qol_fps *f, uint64_t now, struct qol_fps_stats *out)
{
    int i, first = -1, n;
    uint64_t span, worst = 0;
    memset(out, 0, sizeof *out);
    /* frames that ENDED inside the window; the frame before the first one gives its start */
    for (i = 0; i < f->count; i++) {
        uint64_t t = fps_at(f, i);
        if (t <= now && t + QOL_FPS_WINDOW_US > now) {
            first = i;
            break;
        }
    }
    if (first < 0)
        return 0;
    if (first > 0)
        first--;
    n = 0;
    for (i = first + 1; i < f->count && fps_at(f, i) <= now; i++) {
        uint64_t d = fps_at(f, i) - fps_at(f, i - 1);
        if (d > worst)
            worst = d;
        n++;
    }
    if (n < 1)
        return 0;
    span = fps_at(f, first + n) - fps_at(f, first);
    if (span == 0)
        return 0;
    out->frames = n;
    out->fps = (double)n * 1e6 / (double)span;
    out->ms_avg = (double)span / (double)n / 1000.0;
    out->ms_max = (double)worst / 1000.0;
    return 1;
}

void qol_fps_format(const struct qol_fps_stats *s, int have, char *out, size_t n)
{
    if (!have || s->frames < 1)
        snprintf(out, n, "-- FPS");
    else
        snprintf(out, n, "%.1f FPS  %.1f ms  max %.1f", s->fps, s->ms_avg, s->ms_max);
}

/* ---- PNG ---- */

uint32_t qol_crc32(const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    static int ready;
    uint32_t c = 0xFFFFFFFFu;
    size_t i;
    if (!ready) {
        uint32_t k, b, v;
        for (k = 0; k < 256; k++) {
            v = k;
            for (b = 0; b < 8; b++)
                v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[k] = v;
        }
        ready = 1;
    }
    for (i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t qol_adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

#define STORED_MAX 65535u

static size_t raw_size(int w, int h)
{
    return (size_t)h * (1 + (size_t)w * 3);
}

static size_t zlib_size(size_t raw)
{
    size_t blocks = raw ? (raw + STORED_MAX - 1) / STORED_MAX : 1;
    return 2 + blocks * 5 + raw + 4;
}

size_t qol_png_size(int w, int h)
{
    size_t z;
    if (w < 1 || h < 1 || w > 16384 || h > 16384)
        return 0;
    z = zlib_size(raw_size(w, h));
    return 8 + (12 + 13) + (12 + z) + 12;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Chunk at o: length, type, data (already at o + 8), CRC. Returns the bytes used. */
static size_t chunk_close(uint8_t *o, const char *type, size_t len)
{
    be32(o, (uint32_t)len);
    memcpy(o + 4, type, 4);
    be32(o + 8 + len, qol_crc32(o + 4, len + 4));
    return 12 + len;
}

size_t qol_png_encode(const uint8_t *bgra, int w, int h, int stride, uint8_t *out, size_t cap)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    size_t need = qol_png_size(w, h), o = 0, raw = raw_size(w, h), left, z;
    uint8_t *d;
    uint32_t a = 1, b = 0;
    int y = 0, col = -1;   /* col -1 = the row's filter byte next */

    if (!need || cap < need || !bgra)
        return 0;
    memcpy(out, sig, 8);
    o = 8;
    d = out + o + 8;
    be32(d, (uint32_t)w);
    be32(d + 4, (uint32_t)h);
    d[8] = 8;    /* bit depth */
    d[9] = 2;    /* RGB */
    d[10] = 0;   /* deflate */
    d[11] = 0;   /* adaptive filters (all rows use "none") */
    d[12] = 0;   /* no interlace */
    o += chunk_close(out + o, "IHDR", 13);

    z = zlib_size(raw);
    d = out + o + 8;
    d[0] = 0x78;   /* deflate, 32K window */
    d[1] = 0x01;   /* no compression level hint; (0x78 << 8 | 0x01) % 31 == 0 */
    d += 2;
    left = raw;
    do {
        size_t len = left > STORED_MAX ? STORED_MAX : left, i;
        left -= len;
        d[0] = left == 0 ? 1 : 0;   /* BFINAL, BTYPE 00 = stored */
        d[1] = (uint8_t)len;
        d[2] = (uint8_t)(len >> 8);
        d[3] = (uint8_t)~len;
        d[4] = (uint8_t)(~len >> 8);
        d += 5;
        for (i = 0; i < len; i++) {
            uint8_t v;
            if (col < 0) {
                v = 0;   /* filter: none */
                col = 0;
            } else {
                const uint8_t *px = bgra + (size_t)y * (size_t)stride + (size_t)(col / 3) * 4;
                int c = col % 3;
                v = px[2 - c];   /* B G R A -> R G B */
                if (++col == w * 3) {
                    col = -1;
                    y++;
                }
            }
            *d++ = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
        }
    } while (left);
    be32(d, (b << 16) | a);
    o += chunk_close(out + o, "IDAT", z);
    o += chunk_close(out + o, "IEND", 0);
    return o;
}

/* ---- screenshot name ---- */

void qol_shot_name(int year, int mon, int day, int hour, int min, int sec, int seq, char *out, size_t n)
{
    if (seq > 1)
        snprintf(out, n, "breakdown_%04d-%02d-%02d_%02d-%02d-%02d_%d.png", year, mon, day, hour, min, sec, seq);
    else
        snprintf(out, n, "breakdown_%04d-%02d-%02d_%02d-%02d-%02d.png", year, mon, day, hour, min, sec);
}

/* ---- intro skip ---- */

enum qol_intro qol_intro_decide(const char *name, uint32_t flags34, uint32_t mode_cc, float played_s,
                                uint32_t fading_e4)
{
    if (!name || strncmp(name, "opening_b", 9) != 0 || !(flags34 & 1))
        return QOL_INTRO_NONE;
    if (fading_e4 || (mode_cc & 2))
        return QOL_INTRO_LEAVE;
    if (!(played_s > 0.5f))   /* the game's own rule in 0x11880 (also false for NaN) */
        return QOL_INTRO_WAIT;
    return QOL_INTRO_SKIP;
}
