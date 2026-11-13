/* tuning_pad_test.c - unit tests for the tuning range controller map (src/tuning_pad.c).
 * Build: gcc -std=c11 -Wall -Wextra -Werror -I src -o .ai/tuning_pad_test.exe src/tuning_pad_test.c src/tuning_pad.c && .ai/tuning_pad_test.exe */
#include "tuning_pad.h"

#include <stdio.h>
#include <string.h>

static int s_fail;
static void expect(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        s_fail = 1;
}

static unsigned s_t;
static struct tp_result step(unsigned bits, int open)
{
    s_t += 16;
    return tp_step(0, bits, open, s_t);
}
/* Exactly one action, this one. */
static int one(struct tp_result r, int what, int arg)
{
    return r.n == 1 && r.act[0].what == what && r.act[0].arg == arg;
}

int main(void)
{
    unsigned char an[8];
    struct tp_result r;
    int i, rows;

    memset(an, 0, sizeof an);
    an[0] = 255;    /* A */
    an[4] = 200;    /* BLACK = RB */
    an[5] = 30;     /* WHITE below the line */
    an[7] = 64;     /* right trigger at the line */
    expect("bits: d-pad kept, A, RB, RT; light LB not held",
           tp_bits(0x0101u | TP_UP, an) == (TP_UP | TP_A | TP_RB | TP_RT));
    expect("bits: no analog array", tp_bits(TP_BACK, NULL) == TP_BACK);

    tp_reset();
    /* Closed: buttons reach the game, nothing happens. */
    r = step(TP_A | TP_UP, 0);
    expect("closed: no action, nothing hidden", r.n == 0 && r.strip == 0);
    r = step(0, 0);
    /* Back+Start is the PC options page: not ours. */
    r = step(TP_BACK | TP_START, 0);
    expect("Back+Start does not open it", r.n == 0 && r.strip == 0);
    r = step(0, 0);

    /* Open: LS then RS. */
    r = step(TP_LS, 0);
    expect("LS alone does nothing", r.n == 0 && r.strip == 0);
    r = step(TP_LS | TP_RS, 0);
    expect("LS + RS opens", one(r, TPA_TOGGLE, 0) && r.strip == TP_ALL);
    r = step(TP_LS | TP_RS, 1);
    expect("held combo: no second toggle, still hidden", r.n == 0 && r.strip == TP_ALL);
    r = step(0, 1);
    expect("open: game sees no buttons", r.strip == TP_ALL);

    /* Rows and change. */
    r = step(TP_DOWN, 1);
    expect("d-pad down = next row", one(r, TPA_ROW, 1));
    r = step(TP_DOWN, 1);
    expect("held: no repeat yet", r.n == 0);
    for (i = 0, rows = 0; i < 40; i++)   /* 640 ms more held: 400 ms delay, then every 80 ms */
        rows += step(TP_DOWN, 1).n;
    expect("held: repeats after the delay", rows >= 3 && rows <= 4);
    r = step(TP_UP, 1);
    expect("d-pad up = previous row", one(r, TPA_ROW, -1));
    r = step(TP_RIGHT, 1);
    expect("right = +1", one(r, TPA_CHANGE, 1));
    r = step(0, 1);
    r = step(TP_LEFT | TP_RT, 1);
    expect("RT + left = -10", one(r, TPA_CHANGE, -10));
    r = step(0, 1);
    r = step(TP_UP | TP_DOWN, 1);
    expect("up + down together = nothing", r.n == 0);
    r = step(0, 1);

    /* Tabs, ideal, default. */
    r = step(TP_RB, 1);
    expect("RB = next tab", one(r, TPA_TAB, 1));
    r = step(0, 1);
    r = step(TP_LB, 1);
    expect("LB = previous tab", one(r, TPA_TAB, -1));
    r = step(0, 1);
    r = step(TP_Y, 1);
    expect("Y = ideal", one(r, TPA_IDEAL, 0));
    r = step(0, 1);
    r = step(TP_X, 1);
    expect("X = default", one(r, TPA_DEFAULT, 0));
    r = step(0, 1);
    r = step(TP_A, 1);
    expect("A alone does nothing", r.n == 0);
    r = step(0, 1);

    /* Finalize: Start asks, B cancels, Start then A finalizes. */
    r = step(TP_START, 1);
    expect("Start asks to finalize", one(r, TPA_ASK_FINAL, 0) && tp_asking(0));
    r = step(0, 1);
    r = step(TP_DOWN, 1);
    expect("asking: d-pad ignored", r.n == 0 && tp_asking(0));
    r = step(0, 1);
    r = step(TP_B, 1);
    expect("asking: B = no, window stays", one(r, TPA_FINAL_NO, 0) && !tp_asking(0));
    r = step(0, 1);
    r = step(TP_START, 1);
    r = step(0, 1);
    r = step(TP_A, 1);
    expect("asking: A = yes", one(r, TPA_FINAL_YES, 0) && !tp_asking(0));
    r = step(0, 1);
    r = step(TP_START, 1);
    r = step(0, 0);
    expect("closing drops the question", !tp_asking(0));
    r = step(TP_LS | TP_RS, 0);
    r = step(0, 1);

    /* Close with B; B stays hidden from the game until it is let go. */
    r = step(TP_B, 1);
    expect("B closes", one(r, TPA_CLOSE, 0) && r.strip == TP_ALL);
    r = step(TP_B, 0);
    expect("closed, B still held: hidden", r.n == 0 && r.strip == TP_B);
    r = step(TP_B | TP_A, 0);
    expect("new A reaches the game", r.strip == TP_B);
    r = step(TP_A, 0);
    expect("B let go: nothing hidden", r.strip == 0);
    r = step(TP_B, 0);
    expect("fresh B reaches the game", r.strip == 0 && r.n == 0);
    r = step(0, 0);

    /* Close with the combo. */
    r = step(TP_LS | TP_RS, 0);
    r = step(0, 1);
    r = step(TP_LS | TP_RS, 1);
    expect("LS + RS closes", one(r, TPA_TOGGLE, 0));
    r = step(TP_LS | TP_RS, 0);
    expect("combo still held after close: hidden", r.strip == (TP_LS | TP_RS));

    /* Ports are separate. */
    tp_reset();
    tp_step(1, TP_LS | TP_RS, 0, 100);
    r = tp_step(0, TP_LS | TP_RS, 0, 116);
    expect("port 0 opens on its own edge", one(r, TPA_TOGGLE, 0));
    r = tp_step(4, TP_LS | TP_RS, 0, 132);
    expect("bad port ignored", r.n == 0 && r.strip == 0);

    printf(s_fail ? "FAILED\n" : "all passed\n");
    return s_fail;
}
