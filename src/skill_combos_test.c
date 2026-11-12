/* skill_combos_test.c - checks the combo text encoder against codes seen in the game's text bank. */
#include "skill_combos.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    uint16_t out[128];
    struct skill_combo_pics none = { 0, 0 }, pics = { 13, 12 };
    size_t n;
    int i;

    /* "RT , LT": R=0x11B T=0x11D, space 3, comma 0x145 (seen on screen: 0x147 is a colon). */
    n = skill_combo_encode("{A1} , {A2}", &none, out, 128);
    {
        static const uint16_t want[] = { 0x11B, 0x11D, 3, 0x145, 3, 0x115, 0x11D, SC_END };
        CHECK(n == sizeof want / sizeof want[0]);
        CHECK(memcmp(out, want, sizeof want) == 0);
    }
    /* A picture tag is laid out as in the bank: 0x000C, 4, then "guide13\0" as 4 little-endian words. */
    n = skill_combo_encode("{A1} + {A2}", &pics, out, 128);
    {
        static const uint16_t want[] = { 0x0C, 4, 0x7567, 0x6469, 0x3165, 0x0033, 3, 0x14A, 3,
                                         0x0C, 4, 0x7567, 0x6469, 0x3165, 0x0032, SC_END };
        CHECK(n == sizeof want / sizeof want[0]);
        CHECK(memcmp(out, want, sizeof want) == 0);
    }
    /* Lower case, digits, brackets; unknown characters are dropped. */
    n = skill_combo_encode("(fast) 9!", &none, out, 128);
    {
        static const uint16_t want[] = { 0x140, 0x129, 0x124, 0x136, 0x137, 0x142, 3, 0x109, SC_END };
        CHECK(n == sizeof want / sizeof want[0]);
        CHECK(memcmp(out, want, sizeof want) == 0);
    }
    /* Too small: 0. */
    CHECK(skill_combo_encode("{A1} , {A1}", &pics, out, 4) == 0);
    /* Every built-in combo and the test row encode, end with SC_END, and fit a row. */
    CHECK(skill_combo_count() > 0);
    CHECK(skill_combo_spec(-1) == NULL && skill_combo_spec(skill_combo_count()) == NULL);
    for (i = 0; i <= skill_combo_count(); i++) {
        const char *s = i < skill_combo_count() ? skill_combo_spec(i) : skill_combo_test_spec();
        n = skill_combo_encode(s, &pics, out, 96);
        CHECK(n > 1 && out[n - 1] == SC_END);
    }
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("skill_combos_test: all passed\n");
    return 0;
}
