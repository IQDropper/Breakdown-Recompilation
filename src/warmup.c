/* warmup.c - P6.3d shader warm-up state machine (see warmup.h). Pure: no Windows, no game memory. */
#include "warmup.h"

#include <string.h>

static void go(struct warmup *w, enum warmup_state s, uint32_t now)
{
    w->state = s;
    w->since = now;
}

void warmup_init(struct warmup *w, int start, int count, uint32_t now)
{
    memset(w, 0, sizeof *w);
    w->entry = start < 0 ? 0 : start;
    w->left = count < 1 ? 1 : count;
    go(w, WU_BOOT, now);
}

void warmup_area_loaded(struct warmup *w)
{
    /* Areas load in well under a second, so the message can come before the list state ends. */
    if (w->state == WU_LOADING || (w->state == WU_LIST && w->area_seen == 2))
        w->area_seen = 1;
}

void warmup_step(struct warmup *w, uint32_t now, struct warmup_out *o)
{
    uint32_t t = now - w->since;
    memset(o, 0, sizeof *o);
    switch (w->state) {
    case WU_OFF:
        return;
    case WU_BOOT:
        if (t >= WU_BOOT_MS) {
            o->open_menu = 1;
            go(w, WU_MENU1, now);
        }
        return;
    case WU_MENU1:   /* F026 menu: B opens MAKINO MENU */
        if (t >= WU_MENU_MS) {
            go(w, WU_MENU2, now);
            return;
        }
        o->b_pressed = (uint8_t)(t >= WU_MENU_MS - 1000u && t < WU_MENU_MS - 700u);
        return;
    case WU_MENU2:   /* MAKINO MENU: B opens the MAP VIEWER (its cursor is set by warmup_first_entry) */
        if (t >= WU_MENU_MS) {
            go(w, WU_LIST, now);
            return;
        }
        o->b_pressed = (uint8_t)(t >= WU_MENU_MS - 1000u && t < WU_MENU_MS - 700u);
        return;
    case WU_LIST:    /* list shown with the cursor on w->entry: B loads it */
        if (t >= WU_LIST_MS) {
            if (w->area_seen != 1)
                w->area_seen = 0;
            go(w, WU_LOADING, now);
            return;
        }
        o->b_pressed = (uint8_t)(t >= WU_LIST_MS - 1000u && t < WU_LIST_MS - 700u);
        if (o->b_pressed && !w->area_seen)
            w->area_seen = 2;   /* pressed: from now on an area message counts */
        return;
    case WU_LOADING:
        if (w->area_seen == 1 && t >= 500u) {
            w->loaded++;
            go(w, WU_LOOK, now);
        } else if (t >= WU_LOAD_MAX_MS) {
            go(w, WU_STUCK, now);
            o->finished = 1;
        }
        return;
    case WU_LOOK: {  /* settle, then turn right, walk forward, turn left */
        uint32_t m = t > WU_SETTLE_MS ? t - WU_SETTLE_MS : 0;
        if (t < WU_SETTLE_MS) {
            return;
        }
        if (m >= WU_LOOK_MS) {
            o->leave_area = 1;
            go(w, WU_BACK, now);
            return;
        }
        {   /* the map viewer's free camera: d-pad left/right turn, up moves (same as the test walk route) */
            static const struct { uint32_t until; uint16_t pad; } route[] = {
                { 1200u, 0x0008u }, { 4200u, 0x0001u }, { 6700u, 0x0004u }, { 9700u, 0x0001u },
                { 12200u, 0x0008u }, { 15200u, 0x0001u } };
            unsigned i;
            for (i = 0; i < sizeof route / sizeof route[0]; i++)
                if (m < route[i].until) {
                    o->buttons = route[i].pad;
                    break;
                }
        }
        return;
    }
    case WU_BACK:
        if (t >= WU_BACK_MS) {
            w->entry++;
            w->left--;
            if (w->left <= 0) {
                go(w, WU_DONE, now);
                o->finished = 1;
            } else {
                go(w, WU_LIST, now);
            }
        }
        return;
    case WU_DONE:
    case WU_STUCK:
        o->finished = 1;
        return;
    }
}

const char *warmup_state_name(enum warmup_state s)
{
    static const char *const n[] = { "off", "boot", "menu1", "menu2", "list", "loading", "look", "back", "done", "stuck" };
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}
