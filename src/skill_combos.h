/* skill_combos.h - extra rows for the pause menu's Skills list: Derrick's combos, written the game's way
 * (button pictures, "+" = pressed together, "," = one after another). No game memory here: this file turns
 * a combo spec into the game's 16-bit text codes; src/recomp_manual.c puts the result on screen.
 *
 * Spec tokens: {A1} {A2} = Attack 1 / Attack 2 (the script's names; pictures guide13 / guide14), {gNN} = the game's button picture
 * "guideNN" as is. Everything else is text. A token whose button has no known picture is written as the
 * modern pad name instead (RT, LT). */
#ifndef SKILL_COMBOS_H
#define SKILL_COMBOS_H

#include <stddef.h>
#include <stdint.h>

/* The game's text codes (cap_syst_e). */
#define SC_END      0x0002u
#define SC_SPACE    0x0003u
#define SC_TAG      0x000Cu   /* then a word count, then that many words of ASCII (a button picture name) */

/* Which picture ("guideNN") shows each attack button; 0 = none known, so the pad name is written. */
struct skill_combo_pics {
    int attack1, attack2;
};

int skill_combo_count(void);
const char *skill_combo_spec(int i);   /* NULL if out of range */
/* The test row: every picture the game has, numbered, so a screenshot shows which is which. */
const char *skill_combo_test_spec(void);

/* Encode one spec into out (16-bit codes, SC_END last). Returns the number of words written including the end,
 * or 0 if out is too small. Characters the font has no code for are skipped. */
size_t skill_combo_encode(const char *spec, const struct skill_combo_pics *pics, uint16_t *out, size_t cap);

#endif
