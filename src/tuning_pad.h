/*
 * tuning_pad.h - controller map for the tuning range window (no Windows, unit-tested in src/tuning_pad_test.c).
 *
 * Button map (window closed):  LS + RS click (both sticks) opens it.
 * Window open:
 *   D-pad up/down   row            D-pad left/right  change (hold RT for x10; held = repeats)
 *   LB / RB         tab - / +      Y   current -> ideal     X   back to default
 *   B               close          LS + RS click            close
 *   Start           finalize?  ->  A = yes, B or Start = no (a confirm line in the window)
 * While the window is open every button and trigger is kept from the game (sticks still reach it, so the hero can
 * walk while you tune). A button still held when the window closes stays hidden from the game until released.
 * The host side is src/tuning_host.c (tuning_host_pad, called from XInputGetState in src/recomp_manual.c).
 */
#ifndef TUNING_PAD_H
#define TUNING_PAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* One mask for the whole pad. The low byte is the Xbox wButtons byte as is; the analog buttons follow in
 * bAnalogButtons order (A, B, X, Y, BLACK = RB, WHITE = LB, left trigger, right trigger) as 0x100 << index. */
#define TP_UP    0x0001u
#define TP_DOWN  0x0002u
#define TP_LEFT  0x0004u
#define TP_RIGHT 0x0008u
#define TP_START 0x0010u
#define TP_BACK  0x0020u
#define TP_LS    0x0040u
#define TP_RS    0x0080u
#define TP_A     0x0100u
#define TP_B     0x0200u
#define TP_X     0x0400u
#define TP_Y     0x0800u
#define TP_RB    0x1000u
#define TP_LB    0x2000u
#define TP_LT    0x4000u
#define TP_RT    0x8000u
#define TP_ALL   0xFFFFu

#define TP_ANALOG_DOWN 64   /* an analog button or trigger counts as held from this value (0..255) */

enum {
    TPA_TOGGLE = 1,   /* open or close the window */
    TPA_ROW,          /* arg -1 up, +1 down */
    TPA_CHANGE,       /* arg = steps (+-1 or +-10) */
    TPA_TAB,          /* arg -1 / +1 */
    TPA_IDEAL,        /* current -> ideal */
    TPA_DEFAULT,      /* current -> default */
    TPA_CLOSE,
    TPA_ASK_FINAL,    /* show "Finalize? A = yes, B = no" */
    TPA_FINAL_YES,
    TPA_FINAL_NO
};

#define TP_MAX_ACTS 8
#define TP_REPEAT_DELAY_MS 400
#define TP_REPEAT_EVERY_MS 80

struct tp_act { int what, arg; };
struct tp_result {
    int n;
    struct tp_act act[TP_MAX_ACTS];
    unsigned strip;   /* TP_ bits to hide from the game this poll */
};

/* wButtons + bAnalogButtons[8] -> one TP_ mask. */
unsigned tp_bits(unsigned wbuttons, const unsigned char analog[8]);

void tp_reset(void);
/* One poll of one port (0..3). open = the window is shown now; now_ms = any millisecond clock. */
struct tp_result tp_step(int port, unsigned bits, int open, unsigned now_ms);
/* The port is waiting for A / B after Start. */
int tp_asking(int port);

#ifdef __cplusplus
}
#endif
#endif
