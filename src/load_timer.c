/* load_timer.c - RECOMP_LOAD_LOG=1: how long each load takes and how the next 5 seconds play (loading plan step 1,
 * kit docs/LOADING_PLAN.md). It reads the game's own loading messages (they pass through our Debug_Printf,
 * sub_00161680) and the frame ends, and prints:
 *   [LOAD] area park3: 452 ms (data set at 301 ms)
 *   [LOAD] section park3_B: 610 ms
 *   [LOAD] after park3_B: worst frame 48.2 ms, 3 frames over 50 ms in 5 s
 * tools/load_report.py turns a log into a table. Off: nothing runs but one getenv. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "load_timer.h"

static int s_on = -1;
static double s_freq;
static double s_t0;              /* current job start, ms; 0 = none */
static double s_data_ms;         /* area: AREA DATA SET, ms after start */
static char s_kind[12], s_name[64];
static double s_after_until;     /* ms clock; while now < this, collect frame stats */
static char s_after_name[64];
static double s_last_frame, s_worst;
static int s_slow;

static double now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / s_freq;
}

int load_timer_on(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_LOAD_LOG");
        LARGE_INTEGER f;
        s_on = e && *e == '1';
        QueryPerformanceFrequency(&f);
        s_freq = (double)f.QuadPart;
        if (s_on) {
            fprintf(stderr, "[LOAD] timer on: one line per area/section load, then 5 s of frame times\n");
            fflush(stderr);
        }
    }
    return s_on;
}

static void start(const char *kind, const char *name)
{
    s_t0 = now_ms();
    s_data_ms = 0;
    snprintf(s_kind, sizeof s_kind, "%s", kind);
    snprintf(s_name, sizeof s_name, "%s", name);
}

static void finish(void)
{
    double t = now_ms(), ms = t - s_t0;
    if (s_data_ms > 0)
        fprintf(stderr, "[LOAD] %s %s: %.0f ms (data set at %.0f ms)\n", s_kind, s_name, ms, s_data_ms);
    else
        fprintf(stderr, "[LOAD] %s %s: %.0f ms\n", s_kind, s_name, ms);
    fflush(stderr);
    snprintf(s_after_name, sizeof s_after_name, "%s", s_name);
    s_after_until = t + 5000.0;
    s_worst = 0;
    s_slow = 0;
    s_last_frame = t;
    s_t0 = 0;
}

/* "area/park3_B (lev4) Load Start" -> "park3_B"; "area_load area/park3 / BLKA" -> "park3" */
static void name_after(const char *msg, const char *key, char *out, size_t n)
{
    const char *p = strstr(msg, key);
    size_t k = 0;
    if (!p) { snprintf(out, n, "?"); return; }
    p += strlen(key);
    while (*p && *p != ' ' && *p != '/' && *p != '\n' && k + 1 < n)
        out[k++] = *p++;
    out[k] = 0;
}

void load_timer_msg(const char *msg)
{
    char nm[64];
    if (!load_timer_on())
        return;
    if (strstr(msg, "area_load area/")) {                       /* map viewer / Area_Load */
        name_after(msg, "area_load area/", nm, sizeof nm);
        start("area", nm);
    } else if (strstr(msg, "EVENT PAK LOAD END /AreaLoadStart")) {   /* story: the area load proper starts */
        start("area", "(story)");
    } else if (strstr(msg, "AREA_NAME_SET area/") && s_t0 > 0 && strcmp(s_name, "(story)") == 0) {
        name_after(msg, "AREA_NAME_SET area/", nm, sizeof nm);
        snprintf(s_name, sizeof s_name, "%s", nm);
    } else if (strstr(msg, "AREA DATA SET") && s_t0 > 0) {
        s_data_ms = now_ms() - s_t0;
    } else if (strstr(msg, "(lev4) Load Start")) {
        if (s_t0 > 0 && strcmp(s_kind, "section") == 0)
            finish();                                              /* back-to-back sections */
        name_after(msg, "area/", nm, sizeof nm);
        start("section", nm);
    } else if (strstr(msg, "EVENT .evb & .pak Load Finish") && s_t0 > 0) {
        finish();
    }
}

void load_timer_frame(void)
{
    double t;
    if (!s_on || s_after_until <= 0)
        return;
    t = now_ms();
    if (s_last_frame > 0) {
        double dt = t - s_last_frame;
        if (dt > s_worst) s_worst = dt;
        if (dt > 50.0) s_slow++;
    }
    s_last_frame = t;
    if (t >= s_after_until) {
        fprintf(stderr, "[LOAD] after %s: worst frame %.1f ms, %d frames over 50 ms in 5 s\n",
                s_after_name, s_worst, s_slow);
        fflush(stderr);
        s_after_until = 0;
    }
}
