/*
 * speedrun.c - the speedrun timer's model (see speedrun.h). Pure C, tested by src/speedrun_test.c.
 *
 * In-game time (IGT) counts while a run is going, except during loads and while the game is paused.
 * A load starts when the game opens an area pak ("area\<name>\<name>_A.pak") and ends once no file under
 * "area\" has been opened for SPEEDRUN_LOAD_QUIET_MS (capped at SPEEDRUN_LOAD_MAX_MS). Each area pak that is
 * not the same as the last split adds a split; the first one after a reset starts the run.
 */
#include "speedrun.h"

#include <stdio.h>
#include <string.h>

void speedrun_init(struct speedrun *s)
{
    memset(s, 0, sizeof *s);
    s->quiet_ms = SPEEDRUN_LOAD_QUIET_MS;
    s->max_ms = SPEEDRUN_LOAD_MAX_MS;
}

void speedrun_reset(struct speedrun *s)
{
    unsigned q = s->quiet_ms, m = s->max_ms;
    int paused = s->paused;
    speedrun_init(s);
    s->quiet_ms = q;
    s->max_ms = m;
    s->paused = paused;   /* the game's pause flag does not change with our reset */
}

static uint64_t load_end(const struct speedrun *s)
{
    uint64_t a = s->load_last_t + s->quiet_ms, b = s->load_start_t + s->max_ms;
    return a < b ? a : b;
}

void speedrun_update(struct speedrun *s, uint64_t t)
{
    uint64_t from;
    if (t < s->last_t)
        t = s->last_t;   /* clocks do not go back */
    if (s->state != SPEEDRUN_RUNNING) {
        s->last_t = t;
        return;
    }
    from = s->last_t;
    if (s->loading) {
        uint64_t le = load_end(s);
        if (t < le) {
            s->load_ms += t - from;
            s->last_t = t;
            return;
        }
        if (le > from)
            s->load_ms += le - from;
        from = le > from ? le : from;
        s->loading = 0;
    }
    if (s->paused)
        s->pause_ms += t - from;
    else
        s->igt_ms += t - from;
    s->last_t = t;
}

void speedrun_set_paused(struct speedrun *s, int paused, uint64_t t)
{
    speedrun_update(s, t);
    s->paused = paused != 0;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Case-insensitive search for "\area\" (or "/area/"); returns what follows, or NULL. */
static const char *after_area(const char *p)
{
    for (; *p; p++) {
        if ((p[0] == '\\' || p[0] == '/') && lower(p[1]) == 'a' && lower(p[2]) == 'r' && lower(p[3]) == 'e'
            && lower(p[4]) == 'a' && (p[5] == '\\' || p[5] == '/'))
            return p + 6;
    }
    return NULL;
}

int speedrun_area_path(const char *path)
{
    return path && after_area(path) != NULL;
}

int speedrun_area_pak(const char *path, char *out, size_t n)
{
    const char *dir, *slash, *file;
    size_t dlen, flen, i;
    if (!path || !out || !n)
        return 0;
    out[0] = 0;
    dir = after_area(path);
    if (!dir)
        return 0;
    for (slash = dir; *slash && *slash != '\\' && *slash != '/'; slash++)
        ;
    if (!*slash)
        return 0;
    dlen = (size_t)(slash - dir);
    file = slash + 1;
    flen = strlen(file);
    /* file = <name> "_" <one letter> ".pak", nothing else */
    if (dlen == 0 || flen != dlen + 6 || strchr(file, '\\') || strchr(file, '/'))
        return 0;
    for (i = 0; i < dlen; i++)
        if (lower((unsigned char)file[i]) != lower((unsigned char)dir[i]))
            return 0;
    if (file[dlen] != '_' || lower(file[dlen + 1]) < 'a' || lower(file[dlen + 1]) > 'z')
        return 0;
    if (file[dlen + 2] != '.' || lower(file[dlen + 3]) != 'p' || lower(file[dlen + 4]) != 'a'
        || lower(file[dlen + 5]) != 'k')
        return 0;
    if (dlen + 3 > n)
        return 0;
    for (i = 0; i < dlen; i++)
        out[i] = (char)lower((unsigned char)dir[i]);
    out[dlen] = '_';
    out[dlen + 1] = (char)(file[dlen + 1] >= 'a' ? file[dlen + 1] - 32 : file[dlen + 1]);   /* "_A" */
    out[dlen + 2] = 0;
    return 1;
}

static void add_split(struct speedrun *s, const char *area, uint64_t t)
{
    struct speedrun_split *sp;
    if (s->nsplits >= SPEEDRUN_MAX_SPLITS)
        return;
    sp = &s->splits[s->nsplits++];
    snprintf(sp->area, sizeof sp->area, "%s", area);
    sp->igt_ms = s->igt_ms;
    sp->rta_ms = t - s->start_t;
    sp->load_ms = s->load_ms;
}

enum speedrun_event speedrun_on_path(struct speedrun *s, const char *path, uint64_t t)
{
    char area[SPEEDRUN_NAME_LEN];
    int pak;
    enum speedrun_event ev = SPEEDRUN_EV_NONE;
    if (!speedrun_area_path(path))
        return ev;
    speedrun_update(s, t);
    t = s->last_t;
    pak = speedrun_area_pak(path, area, sizeof area);
    if (s->state == SPEEDRUN_READY) {
        if (!pak)
            return ev;
        s->state = SPEEDRUN_RUNNING;
        s->start_t = t;
        s->last_t = t;
        add_split(s, area, t);
        ev = SPEEDRUN_EV_START;
    } else if (pak && (s->nsplits == 0 || strcmp(s->splits[s->nsplits - 1].area, area) != 0)) {
        if (s->nsplits < SPEEDRUN_MAX_SPLITS) {
            add_split(s, area, t);
            ev = SPEEDRUN_EV_SPLIT;
        }
    }
    if (s->loading) {
        s->load_last_t = t;   /* more area files: the load goes on */
    } else if (pak) {
        s->loading = 1;
        s->load_start_t = s->load_last_t = t;
    }
    return ev;
}

uint64_t speedrun_igt(struct speedrun *s, uint64_t t)
{
    speedrun_update(s, t);
    return s->igt_ms;
}

uint64_t speedrun_rta(const struct speedrun *s, uint64_t t)
{
    if (s->state != SPEEDRUN_RUNNING || t < s->start_t)
        return 0;
    return t - s->start_t;
}

int speedrun_loading(const struct speedrun *s, uint64_t t)
{
    return s->state == SPEEDRUN_RUNNING && s->loading && t < load_end(s);
}

void speedrun_format(uint64_t ms, char *out, size_t n)
{
    unsigned long long cs = (unsigned long long)(ms / 10), h, m, sec;
    sec = cs / 100;
    cs %= 100;
    h = sec / 3600;
    m = sec / 60 % 60;
    sec %= 60;
    if (h)
        snprintf(out, n, "%llu:%02llu:%02llu.%02llu", h, m, sec, cs);
    else
        snprintf(out, n, "%llu:%02llu.%02llu", m, sec, cs);
}

static void row(const struct speedrun_split *sp, const struct speedrun_split *prev, int i, const char *run_id,
                char *out, size_t n)
{
    char igt[32];
    uint64_t seg = prev ? sp->igt_ms - prev->igt_ms : sp->igt_ms;
    speedrun_format(sp->igt_ms, igt, sizeof igt);
    snprintf(out, n, "%s,%d,%s,%llu,%s,%llu,%llu,%llu", run_id ? run_id : "", i, sp->area,
             (unsigned long long)sp->igt_ms, igt, (unsigned long long)seg, (unsigned long long)sp->rta_ms,
             (unsigned long long)sp->load_ms);
}

void speedrun_csv_row(const struct speedrun *s, int i, const char *run_id, char *out, size_t n)
{
    if (i < 0 || i >= s->nsplits) {
        if (n)
            out[0] = 0;
        return;
    }
    row(&s->splits[i], i > 0 ? &s->splits[i - 1] : NULL, i, run_id, out, n);
}

void speedrun_csv_end(struct speedrun *s, uint64_t t, const char *run_id, char *out, size_t n)
{
    struct speedrun_split end;
    if (s->state != SPEEDRUN_RUNNING) {
        if (n)
            out[0] = 0;
        return;
    }
    speedrun_update(s, t);
    memset(&end, 0, sizeof end);
    snprintf(end.area, sizeof end.area, "end");
    end.igt_ms = s->igt_ms;
    end.rta_ms = speedrun_rta(s, s->last_t);
    end.load_ms = s->load_ms;
    row(&end, s->nsplits ? &s->splits[s->nsplits - 1] : NULL, s->nsplits, run_id, out, n);
}
