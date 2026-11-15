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
/* P6.2: where a load's time goes, ms per step: pak read (PakLoadStart to the next message), setup (texture and
 * sound registration, up to EventLoad Start), event file (up to the first Evd wait), waits (Evd In/Out
 * "Waiting" until work resumes or Load Finish). */
static double s_mark, s_pak, s_setup, s_event, s_wait;
static double s_part[LT_PARTS];   /* P6.2: setup parts in the current load, ms */
static int s_step;               /* 0 none, 1 pak read, 2 setup, 3 event file, 4 waiting */
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
    s_mark = s_t0;
    s_pak = s_setup = s_event = s_wait = 0;
    memset(s_part, 0, sizeof s_part);
    s_step = 0;
    snprintf(s_kind, sizeof s_kind, "%s", kind);
    snprintf(s_name, sizeof s_name, "%s", name);
}

/* Close the running step at time t and start step 'next'. */
static void step_to(int next, double t)
{
    double d = t - s_mark;
    if (s_step == 1) s_pak += d;
    else if (s_step == 2) s_setup += d;
    else if (s_step == 3) s_event += d;
    else if (s_step == 4) s_wait += d;
    s_step = next;
    s_mark = t;
}

static void finish(void)
{
    double t = now_ms(), ms = t - s_t0;
    if (s_data_ms > 0)
        fprintf(stderr, "[LOAD] %s %s: %.0f ms (data set at %.0f ms)\n", s_kind, s_name, ms, s_data_ms);
    else
        fprintf(stderr, "[LOAD] %s %s: %.0f ms\n", s_kind, s_name, ms);
    step_to(0, t);
    fprintf(stderr, "[LOAD]   steps %s: pak read %.0f, setup %.0f, event file %.0f, waits %.0f, other %.0f ms\n",
            s_name, s_pak, s_setup, s_event, s_wait, ms - s_pak - s_setup - s_event - s_wait);
    fprintf(stderr, "[LOAD]   setup parts %s: textures %.0f, sounds %.0f, models %.0f, scripts %.0f, map ids %.0f ms\n",
            s_name, s_part[LT_TEX], s_part[LT_SOUND], s_part[LT_MODEL], s_part[LT_SCRIPT], s_part[LT_MAPIDS]);
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
    } else if (strstr(msg, "EVENT PACK LOAD END /AreaLoadStart")) {   /* story: the area load proper starts */
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
    if (s_t0 > 0) {                                                   /* P6.2 steps */
        double t = now_ms();
        if (strstr(msg, "PakLoadStart"))
            step_to(1, t);
        else if (s_step == 1 && !strstr(msg, "Open and Read file"))
            step_to(2, t);                                             /* first message after the read */
        if (strstr(msg, "EventLoad Start"))
            step_to(3, t);
        else if (strstr(msg, "Waiting ...."))
            { if (s_step != 4) step_to(4, t); }
        else if (s_step == 4)
            step_to(2, t);                                             /* work again after a wait */
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
        if (dt > 50.0) {
            s_slow++;
            fprintf(stderr, "[LOAD]   slow frame %.1f ms, %.0f ms after %s loaded\n", dt,
                    t - (s_after_until - 5000.0), s_after_name);
        }
    }
    s_last_frame = t;
    if (t >= s_after_until) {
        fprintf(stderr, "[LOAD] after %s: worst frame %.1f ms, %d frames over 50 ms in 5 s\n",
                s_after_name, s_worst, s_slow);
        fflush(stderr);
        s_after_until = 0;
    }
}

double load_timer_now(void)
{
    if (s_freq <= 0)
        load_timer_on();
    return now_ms();
}

void load_timer_part(int part, double ms)
{
    if (s_on > 0 && s_t0 > 0 && part >= 0 && part < LT_PARTS)
        s_part[part] += ms;
}
