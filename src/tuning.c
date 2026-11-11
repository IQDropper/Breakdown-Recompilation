/*
 * tuning.c - the tuning range's model. See tuning.h.
 */
#include "tuning.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define tune_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define tune_mkdir(p) mkdir(p, 0755)
#endif

/* Phase 1 rows: the values the PC layer already reaches (docs/DEV_TOOLS.md). */
static struct tune_row s_rows[] = {
    { "player.max_life",    "Max life",           TUNE_TAB_PLAYER, 10.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "player.life",        "Life (now)",         TUNE_TAB_PLAYER, 10.0f, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "player.max_stamina", "Max stamina",        TUNE_TAB_PLAYER, 10.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "player.stamina",     "Stamina (now)",      TUNE_TAB_PLAYER, 10.0f, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "player.move_speed",  "Move speed x",       TUNE_TAB_PLAYER, 0.05f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
#define NROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

void tuning_reset(void)
{
    int i;
    for (i = 0; i < NROWS; i++) {
        struct tune_row *r = &s_rows[i];
        r->have_cur = r->have_def = r->have_range = r->have_ideal = r->have_fin = 0;
        r->cur = r->def = r->min = r->max = r->ideal = r->fin = 0.0f;
    }
}

int tuning_count(void) { return NROWS; }
struct tune_row *tuning_row(int i) { return i >= 0 && i < NROWS ? &s_rows[i] : NULL; }

int tuning_find(const char *key)
{
    int i;
    for (i = 0; key && i < NROWS; i++)
        if (strcmp(s_rows[i].key, key) == 0)
            return i;
    return -1;
}

static void default_range(struct tune_row *r)
{
    if (r->have_range || !r->have_def)
        return;
    if (r->def > 0.0f) {
        r->min = r->def * 0.25f;
        r->max = r->def * 4.0f;
    } else {
        r->min = 0.0f;
        r->max = 4.0f;
    }
    r->have_range = 1;
}

void tuning_observe(int i, float value)
{
    struct tune_row *r = tuning_row(i);
    if (!r || !isfinite(value))
        return;
    if (!r->have_def) {
        r->def = value;
        r->have_def = 1;
        default_range(r);
    }
    r->cur = value;
    r->have_cur = 1;
}

float tuning_set(int i, float value)
{
    struct tune_row *r = tuning_row(i);
    if (!r || !isfinite(value))
        return r ? r->cur : 0.0f;
    if (r->have_range) {
        if (value < r->min)
            value = r->min;
        if (value > r->max)
            value = r->max;
    }
    r->cur = value;
    r->have_cur = 1;
    return value;
}

float tuning_nudge(int i, int steps)
{
    struct tune_row *r = tuning_row(i);
    if (!r)
        return 0.0f;
    return tuning_set(i, r->cur + r->step * (float)steps);
}

int tuning_colour(int i)
{
    struct tune_row *r = tuning_row(i);
    if (!r || !r->have_cur)
        return TUNE_GREY;
    if (r->have_range && (r->cur < r->min - 1e-6f || r->cur > r->max + 1e-6f))
        return TUNE_RED;
    if (!r->have_ideal)
        return TUNE_YELLOW;
    {
        float ref = fabsf(r->ideal) > 1e-6f ? fabsf(r->ideal) : 1.0f;
        return fabsf(r->cur - r->ideal) <= 0.05f * ref ? TUNE_GREEN : TUNE_YELLOW;
    }
}

float tuning_delta_pct(int i, int *have)
{
    struct tune_row *r = tuning_row(i);
    *have = 0;
    if (!r || !r->have_fin || !r->have_cur || fabsf(r->fin) < 1e-6f)
        return 0.0f;
    *have = 1;
    return (r->cur - r->fin) / fabsf(r->fin) * 100.0f;
}

/* ---- a small reader for flat JSON objects: "key": number  or  "key": {"min": n, "max": n, "ideal": n} ---- */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    if (b)
        b[n] = 0;
    fclose(f);
    return b;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')
        p++;
    return p;
}

/* Reads "string" into out; returns the char after the closing quote or NULL. */
static const char *read_str(const char *p, char *out, size_t n)
{
    size_t k = 0;
    if (*p != '"')
        return NULL;
    p++;
    while (*p && *p != '"') {
        if (k + 1 < n)
            out[k++] = *p;
        p++;
    }
    out[k] = 0;
    return *p == '"' ? p + 1 : NULL;
}

typedef void (*json_cb)(const char *key, const char *field, float v);

/* Walks one top-level object. field is NULL for "key": number. */
static int walk(const char *s, json_cb cb)
{
    const char *p = skip_ws(s);
    char key[64], field[16];
    if (*p != '{')
        return -1;
    p = skip_ws(p + 1);
    while (*p && *p != '}') {
        char *end;
        float v;
        p = read_str(p, key, sizeof key);
        if (!p)
            return -1;
        p = skip_ws(p);
        if (*p != ':')
            return -1;
        p = skip_ws(p + 1);
        if (*p == '{') {
            p = skip_ws(p + 1);
            while (*p && *p != '}') {
                p = read_str(p, field, sizeof field);
                if (!p)
                    return -1;
                p = skip_ws(p);
                if (*p != ':')
                    return -1;
                v = strtof(skip_ws(p + 1), &end);
                if (end == skip_ws(p + 1))
                    return -1;
                cb(key, field, v);
                p = skip_ws(end);
            }
            if (*p != '}')
                return -1;
            p = skip_ws(p + 1);
        } else {
            v = strtof(p, &end);
            if (end == p)
                return -1;
            cb(key, NULL, v);
            p = skip_ws(end);
        }
    }
    return *p == '}' ? 0 : -1;
}

static int load(const char *path, json_cb cb)
{
    char *s = path ? read_file(path) : NULL;
    int rc;
    if (!s)
        return 0;
    rc = walk(s, cb) == 0 ? 1 : -1;
    free(s);
    return rc;
}

static void cb_range(const char *key, const char *field, float v)
{
    struct tune_row *r = tuning_row(tuning_find(key));
    if (!r || !field)
        return;
    if (strcmp(field, "min") == 0) { r->min = v; r->have_range = 1; }
    else if (strcmp(field, "max") == 0) { r->max = v; r->have_range = 1; }
    else if (strcmp(field, "ideal") == 0) { r->ideal = v; r->have_ideal = 1; }
}
static void cb_final(const char *key, const char *field, float v)
{
    struct tune_row *r = tuning_row(tuning_find(key));
    if (r && !field && !r->state) { r->fin = v; r->have_fin = 1; }
}
static void cb_preset(const char *key, const char *field, float v)
{
    int i = tuning_find(key);
    if (i >= 0 && !field && !s_rows[i].state)
        tuning_set(i, v);
}

int tuning_load_ranges(const char *path) { return load(path, cb_range); }
int tuning_load_final(const char *path) { return load(path, cb_final); }
int tuning_load_preset(const char *path) { return load(path, cb_preset); }

int tuning_save_ranges(const char *path)
{
    FILE *f = path ? fopen(path, "w") : NULL;
    int i, first = 1;
    if (!f)
        return -1;
    fputs("{\n", f);
    for (i = 0; i < NROWS; i++) {
        struct tune_row *r = &s_rows[i];
        if (!r->have_range && !r->have_ideal)
            continue;
        fprintf(f, "%s  \"%s\": {", first ? "" : ",\n", r->key);
        first = 0;
        if (r->have_range)
            fprintf(f, "\"min\": %g, \"max\": %g", r->min, r->max);
        if (r->have_ideal)
            fprintf(f, "%s\"ideal\": %g", r->have_range ? ", " : "", r->ideal);
        fputs("}", f);
    }
    fputs("\n}\n", f);
    return fclose(f) == 0 ? 1 : -1;
}

static int write_values(const char *path, int only_changed)
{
    FILE *f = path ? fopen(path, "w") : NULL;
    int i, n = 0;
    if (!f)
        return -1;
    fputs("{\n", f);
    for (i = 0; i < NROWS; i++) {
        struct tune_row *r = &s_rows[i];
        if (r->state || !r->have_cur)
            continue;
        if (only_changed && r->have_def && fabsf(r->cur - r->def) < 1e-6f)
            continue;
        fprintf(f, "%s  \"%s\": %g", n ? ",\n" : "", r->key, r->cur);
        n++;
    }
    fputs("\n}\n", f);
    return fclose(f) == 0 ? n : -1;
}

int tuning_save_values(const char *path) { return write_values(path, 0) < 0 ? -1 : 1; }

int tuning_finalize(const char *dir, const char *changelog, const char *date, const char *note)
{
    char final_path[512], hist_dir[512], hist_path[600], stamp[32];
    char *old;
    int i, n, k = 0;
    FILE *c;
    if (!dir || !date)
        return -1;
    snprintf(final_path, sizeof final_path, "%s/tuning_final.json", dir);
    snprintf(hist_dir, sizeof hist_dir, "%s/history", dir);
    for (i = 0; date[i] && k + 1 < (int)sizeof stamp; i++)   /* "2026-11-11 22:10" -> "2026-11-11_2210" */
        if (date[i] != ':')
            stamp[k++] = date[i] == ' ' ? '_' : date[i];
    stamp[k] = 0;
    old = read_file(final_path);
    if (old) {
        FILE *h;
        tune_mkdir(hist_dir);
        snprintf(hist_path, sizeof hist_path, "%s/tuning_final_%s.json", hist_dir, stamp);
        h = fopen(hist_path, "wb");
        if (!h) {
            free(old);
            return -1;
        }
        fputs(old, h);
        fclose(h);
        free(old);
    }
    n = write_values(final_path, 1);
    if (n < 0)
        return -1;
    if (changelog && (c = fopen(changelog, "a")) != NULL) {
        fprintf(c, "\n## %s%s%s\n", date, note && *note ? ": " : "", note ? note : "");
        for (i = 0; i < NROWS; i++) {
            struct tune_row *r = &s_rows[i];
            if (r->state || !r->have_cur || (r->have_def && fabsf(r->cur - r->def) < 1e-6f))
                continue;
            fprintf(c, "- %s (%s): %g -> %g (game default %g", r->name, r->key,
                    r->have_fin ? r->fin : r->def, r->cur, r->def);
            if (r->have_range)
                fprintf(c, "; range %g..%g", r->min, r->max);
            if (r->have_ideal)
                fprintf(c, "; ideal %g", r->ideal);
            fputs(")\n", c);
        }
        fclose(c);
    }
    for (i = 0; i < NROWS; i++) {
        struct tune_row *r = &s_rows[i];
        r->have_fin = 0;
        if (!r->state && r->have_cur && !(r->have_def && fabsf(r->cur - r->def) < 1e-6f)) {
            r->fin = r->cur;
            r->have_fin = 1;
        }
    }
    return n;
}

int tuning_snapshot(const char *path, const char *date)
{
    FILE *f = path ? fopen(path, "w") : NULL;
    int i;
    if (!f)
        return -1;
    fprintf(f, "# Tuning snapshot %s\n\n| Value | Key | Current | Default | Finalized | Min | Max | Ideal |\n"
               "|---|---|---|---|---|---|---|---|\n", date ? date : "");
    for (i = 0; i < NROWS; i++) {
        struct tune_row *r = &s_rows[i];
        fprintf(f, "| %s | %s | ", r->name, r->key);
        if (r->have_cur) fprintf(f, "%g", r->cur);
        fputs(" | ", f);
        if (r->have_def) fprintf(f, "%g", r->def);
        fputs(" | ", f);
        if (r->have_fin) fprintf(f, "%g", r->fin);
        fputs(" | ", f);
        if (r->have_range) fprintf(f, "%g | %g", r->min, r->max); else fputs(" | ", f);
        fputs(" | ", f);
        if (r->have_ideal) fprintf(f, "%g", r->ideal);
        fputs(" |\n", f);
    }
    return fclose(f) == 0 ? 1 : -1;
}
