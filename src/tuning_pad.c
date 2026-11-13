/* tuning_pad.c - controller map for the tuning range window. See tuning_pad.h for the map. */
#include "tuning_pad.h"

#include <string.h>

struct port {
    unsigned prev, latch;
    int asking;
    int vdir, hdir;            /* held direction being repeated, 0 = none */
    unsigned vnext, hnext;     /* next repeat time */
};
static struct port s_p[4];

unsigned tp_bits(unsigned wbuttons, const unsigned char analog[8])
{
    unsigned b = wbuttons & 0xFFu;
    int i;
    for (i = 0; i < 8; i++)
        if (analog && analog[i] >= TP_ANALOG_DOWN)
            b |= 0x100u << i;
    return b;
}

void tp_reset(void) { memset(s_p, 0, sizeof s_p); }

int tp_asking(int port) { return port >= 0 && port < 4 && s_p[port].asking; }

static void add(struct tp_result *r, int what, int arg)
{
    if (r->n < TP_MAX_ACTS) {
        r->act[r->n].what = what;
        r->act[r->n].arg = arg;
        r->n++;
    }
}

/* -1, 0 or +1 from a pair of opposite buttons held now. */
static int axis(unsigned now, unsigned neg, unsigned pos)
{
    if ((now & pos) && !(now & neg))
        return 1;
    if ((now & neg) && !(now & pos))
        return -1;
    return 0;
}

/* Edge, then repeats while held. Returns the direction to act on this poll, or 0. */
static int repeat(int dir, int *held, unsigned *next, unsigned now_ms)
{
    if (dir != *held) {
        *held = dir;
        *next = now_ms + TP_REPEAT_DELAY_MS;
        return dir;
    }
    if (dir && (int)(now_ms - *next) >= 0) {
        *next = now_ms + TP_REPEAT_EVERY_MS;
        return dir;
    }
    return 0;
}

struct tp_result tp_step(int port, unsigned bits, int open, unsigned now_ms)
{
    struct tp_result r;
    struct port *p;
    unsigned rose;
    int d, toggled = 0;
    memset(&r, 0, sizeof r);
    if (port < 0 || port >= 4)
        return r;
    p = &s_p[port];
    rose = bits & ~p->prev;
    if (!open) {
        p->asking = 0;
        p->vdir = p->hdir = 0;
    }
    if ((rose & (TP_LS | TP_RS)) && (bits & TP_LS) && (bits & TP_RS)) {
        add(&r, TPA_TOGGLE, 0);
        toggled = 1;
        p->asking = 0;
    } else if (open && p->asking) {
        if (rose & TP_A) {
            add(&r, TPA_FINAL_YES, 0);
            p->asking = 0;
        } else if (rose & (TP_B | TP_START)) {
            add(&r, TPA_FINAL_NO, 0);
            p->asking = 0;
        }
    } else if (open) {
        if ((d = repeat(axis(bits, TP_UP, TP_DOWN), &p->vdir, &p->vnext, now_ms)) != 0)
            add(&r, TPA_ROW, d);
        if ((d = repeat(axis(bits, TP_LEFT, TP_RIGHT), &p->hdir, &p->hnext, now_ms)) != 0)
            add(&r, TPA_CHANGE, d * ((bits & TP_RT) ? 10 : 1));
        if ((rose & TP_LB) && !(rose & TP_RB))
            add(&r, TPA_TAB, -1);
        if ((rose & TP_RB) && !(rose & TP_LB))
            add(&r, TPA_TAB, 1);
        if (rose & TP_Y)
            add(&r, TPA_IDEAL, 0);
        if (rose & TP_X)
            add(&r, TPA_DEFAULT, 0);
        if (rose & TP_START) {
            add(&r, TPA_ASK_FINAL, 0);
            p->asking = 1;
        }
        if (rose & TP_B)
            add(&r, TPA_CLOSE, 0);
    }
    /* What the game may see: nothing while open (or opening); after it closes, nothing that is still held. */
    if (open || toggled) {
        r.strip = TP_ALL;
        p->latch = bits;
    } else {
        p->latch &= bits;
        r.strip = p->latch;
    }
    p->prev = bits;
    return r;
}
