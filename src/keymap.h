/* keymap.h - keyboard remapping text (A11Y-1). Pure: no Windows, no toolkit.
 *
 * RECOMP_KEYMAP holds one entry per remapped control, separated by ';':  CONTROL=KEY[|KEY2]
 *   e.g.  "LT=1|Q;RT=3|E;A=SPACE"
 * CONTROL is a short name (A B X Y WHITE BLACK LT RT START BACK LS RS UP DOWN LEFT RIGHT
 * LSUP LSDOWN LSLEFT LSRIGHT RSUP RSDOWN RSLEFT RSRIGHT). KEY is a letter, a digit, F1..F12, or a name
 * (SPACE TAB ENTER BACKSPACE SHIFT CTRL ALT ESC UPARROW DOWNARROW LEFTARROW RIGHTARROW NUM0..NUM9 ...).
 * Controls not listed keep their default keys. The toolkit rebinds rows by their guest name
 * (keymap_guest_name), e.g. "Left trigger". */
#ifndef KEYMAP_H
#define KEYMAP_H

#include <stddef.h>

#define KEYMAP_CONTROLS 24

struct keymap_bind {
    int set;               /* 1 = remapped by the text */
    unsigned char vk[2];   /* Windows virtual-key codes; vk[1] = 0 for none */
};

/* Control index by short name ("LT"), -1 if unknown. Case does not matter. */
int keymap_control(const char *name);
const char *keymap_control_name(int c);   /* "LT" */
const char *keymap_guest_name(int c);     /* the toolkit row's guest name, "Left trigger" */
const char *keymap_control_label(int c);  /* for the page: "Left trigger (Attack 2)" style short label */

/* Key: name <-> virtual-key code. 0 / NULL when unknown. */
int keymap_key(const char *name);
const char *keymap_key_name(int vk);

/* Parse the text into binds[KEYMAP_CONTROLS] (cleared first). Returns the number of entries used; bad entries are
 * skipped and counted in *bad (may be NULL). */
int keymap_parse(const char *text, struct keymap_bind binds[KEYMAP_CONTROLS], int *bad);
/* Write binds back as text (only the set ones, in control order). Returns 0, or -1 if it does not fit. */
int keymap_format(const struct keymap_bind binds[KEYMAP_CONTROLS], char *out, size_t n);

#endif
