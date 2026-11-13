/*
 * a11y_input_test.c - unit test for hold to repeat and hold toggle (src/a11y_input.c).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o a11y_input_test src/a11y_input_test.c src/a11y_input.c
 */
#include "a11y_input.h"

#include <stdio.h>
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

#define HALF A11Y_REPEAT_HALF_MS

/* One poll with only button b at value v; returns what the game sees for b. */
static uint8_t poll1(struct a11y_pad *p, unsigned rep, unsigned tog, int b, uint8_t v, uint32_t t)
{
    uint8_t a[A11Y_BTN_COUNT];
    memset(a, 0, sizeof a);
    a[b] = v;
    a11y_pad_filter(p, rep, tog, a, t);
    return a[b];
}

static void test_off_passes_through(void)
{
    struct a11y_pad p;
    uint8_t a[A11Y_BTN_COUNT] = { 200, 0, 31, 30, 255, 1, 128, 255 }, b[A11Y_BTN_COUNT];
    uint32_t t;
    a11y_pad_reset(&p);
    for (t = 0; t < 1000; t += 16) {
        memcpy(b, a, sizeof b);
        a11y_pad_filter(&p, 0, 0, b, t);
        if (memcmp(a, b, sizeof a)) break;
    }
    expect("off: every button passes through for a second", t >= 1000);
    expect("off masks are zero", a11y_repeat_mask(A11Y_REPEAT_OFF) == 0 && a11y_toggle_mask(A11Y_TOGGLE_OFF) == 0);
    expect("bad choices are off", a11y_repeat_mask(-1) == 0 && a11y_repeat_mask(99) == 0
                                      && a11y_toggle_mask(-1) == 0 && a11y_toggle_mask(99) == 0);
}

static void test_repeat_rate(void)
{
    struct a11y_pad p;
    unsigned rep = a11y_repeat_mask(A11Y_REPEAT_A);
    uint32_t t;
    int presses = 0, was = 0;
    a11y_pad_reset(&p);
    expect("repeat: first poll of a hold is a press", poll1(&p, rep, 0, A11Y_BTN_A, 255, 1000) == 255);
    expect("repeat: still down inside the first half", poll1(&p, rep, 0, A11Y_BTN_A, 255, 1000 + HALF - 1) == 255);
    expect("repeat: up in the second half", poll1(&p, rep, 0, A11Y_BTN_A, 255, 1000 + HALF) == 0);
    expect("repeat: down again next period", poll1(&p, rep, 0, A11Y_BTN_A, 255, 1000 + 2 * HALF) == 255);
    expect("repeat: keeps the real pressure", poll1(&p, rep, 0, A11Y_BTN_A, 140, 1000 + 4 * HALF) == 140);
    expect("repeat: release is release", poll1(&p, rep, 0, A11Y_BTN_A, 0, 1000 + 5 * HALF) == 0);

    /* Count press edges the game would see over one second of holding, polled at 30 FPS. */
    a11y_pad_reset(&p);
    for (t = 0; t < 1000; t += 33) {
        int down = poll1(&p, rep, 0, A11Y_BTN_A, 255, 5000 + t) > A11Y_PRESS_LEVEL;
        if (down && !was) presses++;
        was = down;
    }
    expect("repeat: 10 presses a second at 30 FPS", presses == (int)A11Y_REPEAT_HZ);
    presses = 0; was = 0;
    a11y_pad_reset(&p);
    for (t = 0; t < 1000; t += 16) {
        int down = poll1(&p, rep, 0, A11Y_BTN_A, 255, 9000 + t) > A11Y_PRESS_LEVEL;
        if (down && !was) presses++;
        was = down;
    }
    expect("repeat: 10 presses a second at 60 FPS", presses == (int)A11Y_REPEAT_HZ);
}

static void test_repeat_restarts_on_new_press(void)
{
    struct a11y_pad p;
    unsigned rep = a11y_repeat_mask(A11Y_REPEAT_A);
    a11y_pad_reset(&p);
    poll1(&p, rep, 0, A11Y_BTN_A, 255, 0);
    poll1(&p, rep, 0, A11Y_BTN_A, 0, 10);
    /* 10 + HALF would be an "up" half of the old hold; a new press starts its own period. */
    expect("repeat: a new press is down at once", poll1(&p, rep, 0, A11Y_BTN_A, 255, HALF + 7) == 255);
    expect("repeat: light touch under the press level is untouched", poll1(&p, rep, 0, A11Y_BTN_A, 20, 4 * HALF + 7) == 20);
}

static void test_repeat_choices(void)
{
    struct a11y_pad p;
    uint8_t a[A11Y_BTN_COUNT];
    unsigned face = a11y_repeat_mask(A11Y_REPEAT_FACE);
    int i, ok = 1;
    expect("repeat B mask", a11y_repeat_mask(A11Y_REPEAT_B) == (1u << A11Y_BTN_B));
    expect("repeat X mask", a11y_repeat_mask(A11Y_REPEAT_X) == (1u << A11Y_BTN_X));
    expect("repeat Y mask", a11y_repeat_mask(A11Y_REPEAT_Y) == (1u << A11Y_BTN_Y));
    a11y_pad_reset(&p);
    memset(a, 255, sizeof a);
    a11y_pad_filter(&p, face, 0, a, 0);
    memset(a, 255, sizeof a);
    a11y_pad_filter(&p, face, 0, a, HALF);
    for (i = A11Y_BTN_A; i <= A11Y_BTN_Y; i++) ok &= a[i] == 0;
    expect("repeat face: A B X Y all go up together", ok);
    expect("repeat face: triggers and Black/White untouched",
           a[A11Y_BTN_BLACK] == 255 && a[A11Y_BTN_WHITE] == 255 && a[A11Y_BTN_LT] == 255 && a[A11Y_BTN_RT] == 255);
    a11y_pad_reset(&p);
    expect("repeat A leaves B alone", poll1(&p, a11y_repeat_mask(A11Y_REPEAT_A), 0, A11Y_BTN_B, 255, 0) == 255
                                          && poll1(&p, a11y_repeat_mask(A11Y_REPEAT_A), 0, A11Y_BTN_B, 255, HALF) == 255);
}

static void test_toggle(void)
{
    struct a11y_pad p;
    unsigned tog = a11y_toggle_mask(A11Y_TOGGLE_RT);
    int b = A11Y_BTN_RT;
    a11y_pad_reset(&p);
    expect("toggle: idle is up", poll1(&p, 0, tog, b, 0, 0) == 0);
    expect("toggle: press latches on", poll1(&p, 0, tog, b, 255, 16) == 255);
    expect("toggle: still on while held", poll1(&p, 0, tog, b, 255, 32) == 255);
    expect("toggle: stays on after release", poll1(&p, 0, tog, b, 0, 48) == 255);
    expect("toggle: stays on for a long time", poll1(&p, 0, tog, b, 0, 600000) == 255);
    expect("toggle: half pull under the level does nothing", poll1(&p, 0, tog, b, 25, 600016) == 255);
    expect("toggle: next press lets go", poll1(&p, 0, tog, b, 200, 600032) == 0);
    expect("toggle: stays off while held", poll1(&p, 0, tog, b, 200, 600048) == 0);
    expect("toggle: stays off after release", poll1(&p, 0, tog, b, 0, 600064) == 0);
    expect("toggle: a third press latches again", poll1(&p, 0, tog, b, 255, 600080) == 255);
}

static void test_toggle_choices(void)
{
    struct a11y_pad p;
    uint8_t a[A11Y_BTN_COUNT];
    unsigned both = a11y_toggle_mask(A11Y_TOGGLE_BOTH);
    expect("toggle LT mask", a11y_toggle_mask(A11Y_TOGGLE_LT) == (1u << A11Y_BTN_LT));
    a11y_pad_reset(&p);
    memset(a, 0, sizeof a);
    a[A11Y_BTN_LT] = 255;
    a11y_pad_filter(&p, 0, both, a, 0);
    expect("toggle both: LT latches alone", a[A11Y_BTN_LT] == 255 && a[A11Y_BTN_RT] == 0);
    memset(a, 0, sizeof a);
    a[A11Y_BTN_RT] = 255;
    a11y_pad_filter(&p, 0, both, a, 16);
    expect("toggle both: RT latches too, LT stays", a[A11Y_BTN_LT] == 255 && a[A11Y_BTN_RT] == 255);
    memset(a, 0, sizeof a);
    a[A11Y_BTN_A] = 255;
    a11y_pad_filter(&p, 0, both, a, 32);
    expect("toggle: A passes through", a[A11Y_BTN_A] == 255);
}

static void test_toggle_off_drops_latch(void)
{
    struct a11y_pad p;
    unsigned tog = a11y_toggle_mask(A11Y_TOGGLE_RT);
    a11y_pad_reset(&p);
    poll1(&p, 0, tog, A11Y_BTN_RT, 255, 0);
    poll1(&p, 0, tog, A11Y_BTN_RT, 0, 16);
    expect("toggle off: latch is dropped", poll1(&p, 0, 0, A11Y_BTN_RT, 0, 32) == 0);
    expect("toggle back on: starts unlatched", poll1(&p, 0, tog, A11Y_BTN_RT, 0, 48) == 0);
    expect("toggle off: trigger passes through", poll1(&p, 0, 0, A11Y_BTN_RT, 90, 64) == 90);
}

static void test_toggle_wins_over_repeat(void)
{
    struct a11y_pad p;
    a11y_pad_reset(&p);
    /* Hand-built masks: RT in both. The toggle owns it; no repeat gaps. */
    poll1(&p, 1u << A11Y_BTN_RT, 1u << A11Y_BTN_RT, A11Y_BTN_RT, 255, 0);
    expect("same button: toggle wins", poll1(&p, 1u << A11Y_BTN_RT, 1u << A11Y_BTN_RT, A11Y_BTN_RT, 255, HALF) == 255);
}

static void test_reset_and_nulls(void)
{
    struct a11y_pad p;
    uint8_t a[A11Y_BTN_COUNT] = { 0 };
    unsigned tog = a11y_toggle_mask(A11Y_TOGGLE_RT);
    a11y_pad_reset(&p);
    poll1(&p, 0, tog, A11Y_BTN_RT, 255, 0);
    a11y_pad_reset(&p);   /* pad unplugged */
    expect("reset: latch is gone", poll1(&p, 0, tog, A11Y_BTN_RT, 0, 16) == 0);
    a11y_pad_filter(NULL, 1, 1, a, 0);
    a11y_pad_filter(&p, 1, 1, NULL, 0);
    a11y_pad_reset(NULL);
    expect("null pad or buttons: no crash", 1);
}

static void test_clock_wrap(void)
{
    struct a11y_pad p;
    unsigned rep = a11y_repeat_mask(A11Y_REPEAT_A);
    uint32_t t0 = 0xFFFFFFFFu - 10u;
    a11y_pad_reset(&p);
    poll1(&p, rep, 0, A11Y_BTN_A, 255, t0);
    expect("repeat: clock wrap keeps the rhythm", poll1(&p, rep, 0, A11Y_BTN_A, 255, t0 + HALF) == 0
                                                     && poll1(&p, rep, 0, A11Y_BTN_A, 255, t0 + 2 * HALF) == 255);
}

static void test_tokens(void)
{
    int c, ok = 1;
    for (c = 0; c < A11Y_REPEAT_COUNT; c++) ok &= a11y_repeat_from_token(a11y_repeat_token(c)) == c;
    for (c = 0; c < A11Y_TOGGLE_COUNT; c++) ok &= a11y_toggle_from_token(a11y_toggle_token(c)) == c;
    expect("tokens round trip", ok);
    expect("tokens ignore case", a11y_repeat_from_token("ABXY") == A11Y_REPEAT_FACE && a11y_toggle_from_token("RT") == A11Y_TOGGLE_RT);
    expect("unknown or missing token is off", a11y_repeat_from_token("turbo") == A11Y_REPEAT_OFF
                                                  && a11y_repeat_from_token(NULL) == A11Y_REPEAT_OFF
                                                  && a11y_toggle_from_token("") == A11Y_TOGGLE_OFF
                                                  && a11y_toggle_from_token("r") == A11Y_TOGGLE_OFF);
    expect("labels", strcmp(a11y_repeat_label(A11Y_REPEAT_OFF), "Off") == 0
                         && strcmp(a11y_toggle_label(A11Y_TOGGLE_RT), "Right trigger") == 0);
}

int main(void)
{
    test_off_passes_through();
    test_repeat_rate();
    test_repeat_restarts_on_new_press();
    test_repeat_choices();
    test_toggle();
    test_toggle_choices();
    test_toggle_off_drops_latch();
    test_toggle_wins_over_repeat();
    test_reset_and_nulls();
    test_clock_wrap();
    test_tokens();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    printf("all a11y_input tests passed\n");
    return 0;
}
