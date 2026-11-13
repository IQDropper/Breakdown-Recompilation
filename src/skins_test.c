/* skins_test.c - unit test for skins.c. Build: gcc -I src src/skins_test.c src/skins.c */
#include "skins.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    CHECK(skins_parse(NULL) == SKINS_GAME);
    CHECK(skins_parse("") == SKINS_GAME);
    CHECK(skins_parse("Game") == SKINS_GAME);
    CHECK(skins_parse("normal") == 0);
    CHECK(skins_parse("0") == 0);
    CHECK(skins_parse("T") == 4);
    CHECK(skins_parse("4") == 4);
    CHECK(skins_parse("Researcher") == 3);
    CHECK(skins_parse("lab") == 3);
    CHECK(skins_parse("3") == 3);
    CHECK(skins_parse("pirate") == SKINS_GAME);

    CHECK(skins_choose(0, SKINS_GAME, 1) == 0);   /* no choice */
    CHECK(skins_choose(0, 4, 1) == 4);            /* normal replaced */
    CHECK(skins_choose(0, 4, 0) == 0);            /* models not loaded: stay normal */
    CHECK(skins_choose(3, 4, 1) == 3);            /* story outfit wins */
    CHECK(skins_choose(4, 0, 1) == 4);            /* story "T" wins over a "normal" choice */
    CHECK(skins_choose(0, 0, 1) == 0);

    CHECK(strcmp(skins_label(4), "T shirt") == 0);
    CHECK(strcmp(skins_label(SKINS_GAME), "game's choice") == 0);

    if (fails) {
        printf("skins_test: %d failed\n", fails);
        return 1;
    }
    printf("skins_test: all passed\n");
    return 0;
}
