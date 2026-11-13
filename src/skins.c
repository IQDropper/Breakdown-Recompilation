/* skins.c - hero outfit choice (S7.2). See skins.h. */
#include "skins.h"
#include <ctype.h>
#include <string.h>

static int same(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++, b++;
    }
    return *a == *b;
}

int skins_parse(const char *s)
{
    if (!s || !*s || same(s, "game"))
        return SKINS_GAME;
    if (same(s, "normal") || same(s, "0"))
        return 0;
    if (same(s, "t") || same(s, "4"))
        return 4;
    if (same(s, "researcher") || same(s, "lab") || same(s, "3"))
        return 3;
    return SKINS_GAME;
}

int skins_choose(int game_tag, int want, int loaded)
{
    if (want == SKINS_GAME || game_tag != 0 || !loaded)
        return game_tag;
    return want;
}

const char *skins_label(int tag)
{
    switch (tag) {
    case SKINS_GAME: return "game's choice";
    case 0: return "normal";
    case 4: return "T shirt";
    case 3: return "researcher";
    default: return "other";
    }
}
