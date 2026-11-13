/*
 * a11y_input.h - accessibility options for the pad state the game reads (A11Y-2, A11Y-3).
 *
 * Hold to repeat (A11Y-2): while a chosen face button is held, the game sees it pressed and released
 * at a steady rate (A11Y_REPEAT_HZ), so button-mashing prompts need only a hold.
 * Hold toggle (A11Y-3): one press of a chosen trigger latches it held; the next press lets it go.
 *
 * Both are off by default. Saved in breakdown_video.cfg as RECOMP_A11Y_REPEAT and RECOMP_A11Y_TOGGLE,
 * shown on the PC settings page. Works on the 8 analog buttons of the Xbox pad state
 * (A, B, X, Y, Black, White, LT, RT), so a keyboard key mapped to A repeats too.
 *
 * Pure C, no host calls: the caller passes the time in milliseconds.
 */
#ifndef A11Y_INPUT_H
#define A11Y_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Analog button indexes in XINPUT_GAMEPAD.bAnalogButtons (toolkit xinput_xbox.h). */
enum {
    A11Y_BTN_A = 0, A11Y_BTN_B, A11Y_BTN_X, A11Y_BTN_Y,
    A11Y_BTN_BLACK, A11Y_BTN_WHITE, A11Y_BTN_LT, A11Y_BTN_RT,
    A11Y_BTN_COUNT
};

/* XAPI's crosstalk limit: an analog button above this counts as pressed. */
#define A11Y_PRESS_LEVEL 30u
/* Presses per second while a repeat button is held. Half of each period is "down". At 30 FPS the game
 * polls every ~33 ms, so each 50 ms half is seen at least once; at 60 FPS every ~17 ms. */
#define A11Y_REPEAT_HZ 10u
#define A11Y_REPEAT_HALF_MS (1000u / (A11Y_REPEAT_HZ * 2u))

/* Row choices (the index is what struct video_settings stores). */
enum {
    A11Y_REPEAT_OFF = 0,
    A11Y_REPEAT_A,
    A11Y_REPEAT_B,
    A11Y_REPEAT_X,
    A11Y_REPEAT_Y,
    A11Y_REPEAT_FACE,      /* A, B, X and Y */
    A11Y_REPEAT_COUNT
};
enum {
    A11Y_TOGGLE_OFF = 0,
    A11Y_TOGGLE_RT,
    A11Y_TOGGLE_LT,
    A11Y_TOGGLE_BOTH,      /* each trigger latches on its own */
    A11Y_TOGGLE_COUNT
};

/* Per-port state. Zero it (or call a11y_pad_reset) before the first poll and when a pad goes away. */
struct a11y_pad {
    uint8_t  was_down;                 /* raw pressed bits at the last poll */
    uint8_t  latched;                  /* toggle buttons latched on */
    uint32_t since[A11Y_BTN_COUNT];    /* ms when a repeat button went down */
};

void a11y_pad_reset(struct a11y_pad *p);

/* Bit per analog button (1u << A11Y_BTN_x). Out-of-range choices give 0 (off). */
unsigned a11y_repeat_mask(int choice);
unsigned a11y_toggle_mask(int choice);

/* Change analog[8] in place to what the game should see.
 * Toggle buttons: a press edge flips the latch; the output is 255 while latched, else 0.
 * Repeat buttons (not also toggle buttons): from the press, the output keeps the real value for
 * A11Y_REPEAT_HALF_MS, then 0 for the same time, and so on while held. Released = unchanged.
 * Other buttons pass through. Turning a toggle off (bit leaves the mask) drops its latch. */
void a11y_pad_filter(struct a11y_pad *p, unsigned repeat_mask, unsigned toggle_mask,
                     uint8_t analog[A11Y_BTN_COUNT], uint32_t now_ms);

/* cfg tokens and page text. Inline so video_config.c and display_host.c need no extra source file. */
static inline const char *a11y_repeat_token(int c)
{
    switch (c) {
    case A11Y_REPEAT_A: return "a";
    case A11Y_REPEAT_B: return "b";
    case A11Y_REPEAT_X: return "x";
    case A11Y_REPEAT_Y: return "y";
    case A11Y_REPEAT_FACE: return "abxy";
    default: return "off";
    }
}
static inline const char *a11y_repeat_label(int c)
{
    switch (c) {
    case A11Y_REPEAT_A: return "A";
    case A11Y_REPEAT_B: return "B";
    case A11Y_REPEAT_X: return "X";
    case A11Y_REPEAT_Y: return "Y";
    case A11Y_REPEAT_FACE: return "A B X Y";
    default: return "Off";
    }
}
static inline const char *a11y_toggle_token(int c)
{
    switch (c) {
    case A11Y_TOGGLE_RT: return "rt";
    case A11Y_TOGGLE_LT: return "lt";
    case A11Y_TOGGLE_BOTH: return "both";
    default: return "off";
    }
}
static inline const char *a11y_toggle_label(int c)
{
    switch (c) {
    case A11Y_TOGGLE_RT: return "Right trigger";
    case A11Y_TOGGLE_LT: return "Left trigger";
    case A11Y_TOGGLE_BOTH: return "Both triggers";
    default: return "Off";
    }
}
/* Unknown or missing text = off. Case of the letters does not matter. */
static inline int a11y_token_eq(const char *s, const char *t)
{
    for (; *s && *t; s++, t++) {
        char a = *s;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != *t) return 0;
    }
    return *s == 0 && *t == 0;
}
static inline int a11y_repeat_from_token(const char *s)
{
    int c;
    if (!s) return A11Y_REPEAT_OFF;
    for (c = 0; c < A11Y_REPEAT_COUNT; c++)
        if (a11y_token_eq(s, a11y_repeat_token(c))) return c;
    return A11Y_REPEAT_OFF;
}
static inline int a11y_toggle_from_token(const char *s)
{
    int c;
    if (!s) return A11Y_TOGGLE_OFF;
    for (c = 0; c < A11Y_TOGGLE_COUNT; c++)
        if (a11y_token_eq(s, a11y_toggle_token(c))) return c;
    return A11Y_TOGGLE_OFF;
}

#ifdef __cplusplus
}
#endif

#endif /* A11Y_INPUT_H */
