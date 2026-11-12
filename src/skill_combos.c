/* skill_combos.c - see skill_combos.h. */
#include "skill_combos.h"

#include <string.h>

/* From the hero script bhb_mo_hero (state names SudeTachiAttack1Attack1 ... SudeTachiAttack1andAttack2), in the
 * order a player learns them: two-hit chains, three, four, then the timing variants and both-at-once. */
static const char *const k_combos[] = {
    "{A1} , {A1}",
    "{A1} , {A2}",
    "{A2} , {A1}",
    "{A2} , {A2}",
    "{A2} , {A1} , {A1}",
    "{A2} , {A1} , {A2}",
    "{A2} , {A2} , {A1}",
    "{A2} , {A2} , {A2}",
    "{A2} , {A1} , {A2} , {A1}",
    "{A1} , {A1} , {A2}  (fast)",
    "{A2} , {A2} , {A1}  (slow)",
    "{A1} + {A2}",
};

int skill_combo_count(void)
{
    return (int)(sizeof k_combos / sizeof k_combos[0]);
}

const char *skill_combo_spec(int i)
{
    return (i >= 0 && i < skill_combo_count()) ? k_combos[i] : NULL;
}

const char *skill_combo_test_spec(void)
{
    return "11{g11} 12{g12} 13{g13} 14{g14} 15{g15}";
}

static uint16_t char_code(char c)
{
    if (c >= 'a' && c <= 'z') return (uint16_t)(c + 0xC3);
    if (c >= 'A' && c <= 'Z') return (uint16_t)(c + 0xC9);
    if (c >= '0' && c <= '9') return (uint16_t)(0x100 + (c - '0'));
    switch (c) {
    case ' ':  return SC_SPACE;
    case '.':  return 0x146;
    case ',':  return 0x145;
    case ':':  return 0x147;
    case '+':  return 0x14A;
    case '-':  return 0x14B;
    case '/':  return 0x14C;
    case '?':  return 0x148;
    case '(':  return 0x140;
    case ')':  return 0x142;
    case '\'': return 0x13E;
    default:   return 0;
    }
}

struct sink { uint16_t *out; size_t cap, n; int full; };

static void put(struct sink *s, uint16_t w)
{
    if (s->n < s->cap) s->out[s->n++] = w;
    else s->full = 1;
}

static void put_text(struct sink *s, const char *t)
{
    for (; *t; t++) {
        uint16_t c = char_code(*t);
        if (c) put(s, c);
    }
}

/* "guideNN": 7 ASCII bytes + NUL = 4 words, low byte first. */
static void put_pic(struct sink *s, int n)
{
    char name[8] = "guide00";
    int i;
    name[5] = (char)('0' + (n / 10) % 10);
    name[6] = (char)('0' + n % 10);
    put(s, SC_TAG);
    put(s, 4);
    for (i = 0; i < 8; i += 2)
        put(s, (uint16_t)((uint8_t)name[i] | ((uint16_t)(uint8_t)name[i + 1] << 8)));
}

size_t skill_combo_encode(const char *spec, const struct skill_combo_pics *pics, uint16_t *out, size_t cap)
{
    struct sink s = { out, cap, 0, 0 };
    const char *p = spec;
    while (p && *p) {
        if (*p == '{') {
            const char *e = strchr(p, '}');
            if (e) {
                size_t len = (size_t)(e - p - 1);
                if (len == 2 && p[1] == 'A' && (p[2] == '1' || p[2] == '2')) {
                    int pic = pics ? (p[2] == '1' ? pics->attack1 : pics->attack2) : 0;
                    if (pic) put_pic(&s, pic);
                    else put_text(&s, p[2] == '1' ? "RT" : "LT");
                } else if (len == 3 && p[1] == 'g' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9') {
                    put_pic(&s, (p[2] - '0') * 10 + (p[3] - '0'));
                }
                p = e + 1;
                continue;
            }
        }
        {
            uint16_t c = char_code(*p);
            if (c) put(&s, c);
        }
        p++;
    }
    put(&s, SC_END);
    return s.full ? 0 : s.n;
}
