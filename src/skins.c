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

static const int s_tags[SKINS_CHOICES] = { SKINS_GAME, 0, 4, 3 };
static const char *const s_tokens[SKINS_CHOICES] = { "game", "normal", "t", "researcher" };
static const char *const s_labels[SKINS_CHOICES] = { "Game's choice", "Normal", "T shirt", "Researcher" };

int skins_choice_tag(int choice)
{
    return choice > 0 && choice < SKINS_CHOICES ? s_tags[choice] : SKINS_GAME;
}

int skins_choice_from_token(const char *s)
{
    int tag = skins_parse(s), i;
    for (i = 1; i < SKINS_CHOICES; i++)
        if (s_tags[i] == tag)
            return i;
    return 0;
}

const char *skins_choice_token(int choice)
{
    return s_tokens[choice > 0 && choice < SKINS_CHOICES ? choice : 0];
}

const char *skins_choice_label(int choice)
{
    return s_labels[choice > 0 && choice < SKINS_CHOICES ? choice : 0];
}
