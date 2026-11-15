/* warmup_host.c - P6.3d shader warm-up, the game side (see warmup.h). */
#include "warmup.h"
#include "play_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

static int s_on = -1;
static struct warmup s_w;
static struct warmup_out s_out;
static volatile int s_menu_req, s_f8_req;
static int s_first_used;
static char s_area[64], s_block[64];

int warmup_on(void)
{
    if (s_on < 0) {
        const char *e = getenv("RECOMP_WARMUP");
        s_on = e && *e == '1';
        if (s_on) {
            const char *a = getenv("RECOMP_WARMUP_START"), *b = getenv("RECOMP_WARMUP_COUNT");
            int start = a ? atoi(a) : 0, count = b ? atoi(b) : 20;
            warmup_init(&s_w, start, count, GetTickCount());
            fprintf(stderr, "[WARMUP] on: map-list entries %d..%d (shader cache warm-up)\n", start, start + count - 1);
            fflush(stderr);
        }
    }
    return s_on;
}

void warmup_frame(void)
{
    enum warmup_state before;
    if (!warmup_on())
        return;
    before = s_w.state;
    warmup_step(&s_w, GetTickCount(), &s_out);
    if (s_out.open_menu)
        s_menu_req = 1;
    if (s_out.leave_area)
        s_f8_req = 1;
    if (s_w.state != before && s_w.state == WU_LOOK) {
        fprintf(stderr, "[WARMUP] loaded entry %d (%s %s)\n", s_w.entry, s_area, s_block);
        fflush(stderr);
    }
    if (s_out.finished) {
        fprintf(stderr, "[WARMUP] %s: %d entries loaded, next entry %d\n",
                s_w.state == WU_STUCK ? "stuck (an area did not load)" : "batch done", s_w.loaded,
                s_w.state == WU_STUCK ? s_w.entry + 1 : s_w.entry);
        fflush(stderr);
        ExitProcess(s_w.state == WU_STUCK ? 3 : 0);
    }
}

void warmup_msg(const char *msg)
{
    char a[64], b[64];
    if (!warmup_on() || !strstr(msg, "AREA DATA SET"))
        return;
    if (pr_area_from_msg(msg, a, sizeof a, b, sizeof b)) {
        snprintf(s_area, sizeof s_area, "%s", a);
        snprintf(s_block, sizeof s_block, "%s", b);
    }
    warmup_area_loaded(&s_w);
}

void warmup_pad(uint16_t *buttons, uint8_t analog[8], int16_t thumb[4])
{
    if (!warmup_on())
        return;
    *buttons |= s_out.buttons;
    if (s_out.b_pressed)
        analog[1] = 255;   /* XBOX_BUTTON_B */
    if (s_out.look_x)
        thumb[2] = s_out.look_x;   /* right stick X */
}

int warmup_take_menu(void)
{
    if (!warmup_on() || !s_menu_req)
        return 0;
    s_menu_req = 0;
    return 1;
}

int warmup_take_f8(void)
{
    if (!warmup_on() || !s_f8_req)
        return 0;
    s_f8_req = 0;
    return 1;
}

int warmup_first_entry(void)
{
    if (!warmup_on() || s_first_used)
        return -1;
    s_first_used = 1;
    return s_w.entry;
}
