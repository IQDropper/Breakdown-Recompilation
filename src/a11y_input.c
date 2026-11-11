/* a11y_input.c - hold to repeat and hold toggle on the pad's analog buttons. See a11y_input.h. */
#include "a11y_input.h"

#include <string.h>

#define BIT(b) (1u << (b))
#define FACE (BIT(A11Y_BTN_A) | BIT(A11Y_BTN_B) | BIT(A11Y_BTN_X) | BIT(A11Y_BTN_Y))

void a11y_pad_reset(struct a11y_pad *p)
{
    if (p)
        memset(p, 0, sizeof *p);
}

unsigned a11y_repeat_mask(int choice)
{
    switch (choice) {
    case A11Y_REPEAT_A: return BIT(A11Y_BTN_A);
    case A11Y_REPEAT_B: return BIT(A11Y_BTN_B);
    case A11Y_REPEAT_X: return BIT(A11Y_BTN_X);
    case A11Y_REPEAT_Y: return BIT(A11Y_BTN_Y);
    case A11Y_REPEAT_FACE: return FACE;
    default: return 0;
    }
}

unsigned a11y_toggle_mask(int choice)
{
    switch (choice) {
    case A11Y_TOGGLE_RT: return BIT(A11Y_BTN_RT);
    case A11Y_TOGGLE_LT: return BIT(A11Y_BTN_LT);
    case A11Y_TOGGLE_BOTH: return BIT(A11Y_BTN_LT) | BIT(A11Y_BTN_RT);
    default: return 0;
    }
}

void a11y_pad_filter(struct a11y_pad *p, unsigned repeat_mask, unsigned toggle_mask,
                     uint8_t analog[A11Y_BTN_COUNT], uint32_t now_ms)
{
    unsigned b, down = 0;
    if (!p || !analog)
        return;
    toggle_mask &= 0xFFu;
    repeat_mask &= 0xFFu & ~toggle_mask;
    p->latched &= (uint8_t)toggle_mask;
    for (b = 0; b < A11Y_BTN_COUNT; b++) {
        unsigned bit = BIT(b);
        int is_down = analog[b] > A11Y_PRESS_LEVEL;
        int edge = is_down && !(p->was_down & bit);
        if (is_down)
            down |= bit;
        if (toggle_mask & bit) {
            if (edge)
                p->latched ^= (uint8_t)bit;
            analog[b] = (p->latched & bit) ? 255u : 0u;
        } else if (repeat_mask & bit) {
            if (edge)
                p->since[b] = now_ms;
            if (is_down && ((now_ms - p->since[b]) / A11Y_REPEAT_HALF_MS) & 1u)
                analog[b] = 0;
        }
    }
    p->was_down = (uint8_t)down;
}
