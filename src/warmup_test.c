/*
 * warmup_test.c - unit test for src/warmup.c (P6.3d shader warm-up state machine).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o warmup_test src/warmup_test.c src/warmup.c
 */
#include "warmup.h"

#include <stdio.h>

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

/* Run from now to now+ms in 50 ms steps; return how many frames had B held, and OR the one-shot flags. */
static int run(struct warmup *w, uint32_t *now, uint32_t ms, struct warmup_out *acc)
{
    struct warmup_out o;
    uint32_t end = *now + ms;
    int b = 0;
    acc->open_menu = acc->leave_area = acc->finished = 0;
    acc->look_x = 0;
    acc->buttons = 0;
    for (; *now < end; *now += 50) {
        warmup_step(w, *now, &o);
        b += o.b_pressed;
        acc->open_menu |= o.open_menu;
        acc->leave_area |= o.leave_area;
        acc->finished |= o.finished;
        acc->buttons |= o.buttons;
        if (o.look_x)
            acc->look_x = o.look_x;
    }
    return b;
}

int main(void)
{
    struct warmup w;
    struct warmup_out a;
    uint32_t now = 1000;

    warmup_init(&w, 5, 2, now);
    expect("starts at boot", w.state == WU_BOOT && w.entry == 5 && w.left == 2);
    run(&w, &now, WU_BOOT_MS - 100, &a);
    expect("no menu during boot", !a.open_menu && w.state == WU_BOOT);
    run(&w, &now, 200, &a);
    expect("menu opened after boot", a.open_menu && w.state == WU_MENU1);
    expect("menu1 presses B", run(&w, &now, WU_MENU_MS + 50, &a) > 0 && w.state == WU_MENU2);
    expect("menu2 presses B", run(&w, &now, WU_MENU_MS + 50, &a) > 0 && w.state == WU_LIST);
    expect("list presses B", run(&w, &now, WU_LIST_MS + 50, &a) > 0 && w.state == WU_LOADING);
    {   /* a fast area: the message comes while the list state still runs */
        struct warmup f = w;
        f.state = WU_LIST; f.since = now; f.area_seen = 0;
        uint32_t t2 = now;
        run(&f, &t2, WU_LIST_MS - 800, &a);
        warmup_area_loaded(&f);
        run(&f, &t2, 1500, &a);
        expect("fast area counts", f.state == WU_LOOK && f.loaded == 1);
    }
    expect("presses B again while waiting", run(&w, &now, 5000, &a) > 0);
    expect("waits for the area", w.state == WU_LOADING && w.loaded == 0);
    warmup_area_loaded(&w);
    run(&w, &now, 600, &a);
    expect("area loaded", w.state == WU_LOOK && w.loaded == 1);
    run(&w, &now, WU_SETTLE_MS + WU_LOOK_MS - 1000, &a);
    expect("turns and walks", (a.buttons & 0x0Du) == 0x0Du && !a.leave_area);
    run(&w, &now, 1500, &a);
    expect("F8 after the look", a.leave_area && w.state == WU_BACK);
    run(&w, &now, WU_BACK_MS + 50, &a);
    expect("next entry", w.state == WU_LIST && w.entry == 6 && w.left == 1);
    run(&w, &now, WU_LIST_MS + 50, &a);
    warmup_area_loaded(&w);
    run(&w, &now, 600 + WU_SETTLE_MS + WU_LOOK_MS + WU_BACK_MS + 200, &a);
    expect("batch done", w.state == WU_DONE && a.finished && w.loaded == 2);

    warmup_init(&w, 0, 3, now = 0);
    w.state = WU_LOADING;
    w.since = 0;
    run(&w, &now, WU_LOAD_MAX_MS + 100, &a);
    expect("stuck area stops", w.state == WU_STUCK && a.finished);
    warmup_area_loaded(&w);
    expect("late area ignored", w.state == WU_STUCK);
    expect("names", warmup_state_name(WU_LOOK)[0] == 'l');
    {   /* the map viewer opened a different entry than planned: follow it */
        struct warmup r;
        uint32_t t3 = 0;
        warmup_init(&r, 4, 3, 0);
        r.state = WU_LIST; r.since = 0;
        warmup_map_opened(&r, 0);
        run(&r, &t3, WU_LIST_MS + 700, &a);
        expect("resync to the opened map", r.state == WU_LOOK && r.entry == 0);
        warmup_map_opened(&r, 9);
        expect("ignored outside list/loading", r.entry == 0);
    }

    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    printf("all ok\n");
    return 0;
}
