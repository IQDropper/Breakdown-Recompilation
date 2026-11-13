/* keymap_test.c - unit test for keymap.c. Build: gcc -I src src/keymap_test.c src/keymap.c */
#include "keymap.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    struct keymap_bind b[KEYMAP_CONTROLS], b2[KEYMAP_CONTROLS];
    char out[512];
    int bad, c;

    CHECK(keymap_control("lt") == keymap_control("LT") && keymap_control("LT") >= 0);
    CHECK(keymap_control("jump") == -1);
    CHECK(strcmp(keymap_guest_name(keymap_control("LT")), "Left trigger") == 0);
    CHECK(strcmp(keymap_guest_name(keymap_control("LSUP")), "Left stick up") == 0);
    CHECK(strcmp(keymap_guest_name(keymap_control("START")), "START") == 0);
    for (c = 0; c < KEYMAP_CONTROLS; c++)
        CHECK(keymap_control(keymap_control_name(c)) == c && keymap_guest_name(c)[0] && keymap_control_label(c)[0]);

    CHECK(keymap_key("q") == 'Q' && keymap_key("7") == '7');
    CHECK(keymap_key("F1") == 0x70 && keymap_key("f12") == 0x7B && keymap_key("F13") == 0 && keymap_key("F1x") == 0);
    CHECK(keymap_key("space") == 0x20 && keymap_key("NUM8") == 0x68 && keymap_key("bogus") == 0 && keymap_key("") == 0);
    CHECK(strcmp(keymap_key_name(0x20), "SPACE") == 0 && strcmp(keymap_key_name('Q'), "Q") == 0);
    CHECK(strcmp(keymap_key_name(0x71), "F2") == 0 && keymap_key_name(0x07) == NULL);

    CHECK(keymap_parse("LT=1|Q; RT = 3|E ;A=SPACE", b, &bad) == 3 && bad == 0);
    c = keymap_control("LT");
    CHECK(b[c].set && b[c].vk[0] == '1' && b[c].vk[1] == 'Q');
    c = keymap_control("A");
    CHECK(b[c].set && b[c].vk[0] == 0x20 && b[c].vk[1] == 0);
    CHECK(!b[keymap_control("B")].set);

    CHECK(keymap_parse("LT=nothing;JUMP=Z;A;B=X|??;Y=V", b, &bad) == 1 && bad == 4);
    CHECK(b[keymap_control("Y")].set && b[keymap_control("Y")].vk[0] == 'V');
    CHECK(keymap_parse(NULL, b, &bad) == 0 && bad == 0);
    CHECK(keymap_parse("A=Z|Z", b, NULL) == 1 && b[keymap_control("A")].vk[1] == 0);   /* same key twice = one */

    keymap_parse("RT=3|E;LT=1|Q;LSUP=UPARROW", b, NULL);
    CHECK(keymap_format(b, out, sizeof out) == 0);
    CHECK(strcmp(out, "LT=1|Q;RT=3|E;LSUP=UPARROW") == 0);
    CHECK(keymap_parse(out, b2, &bad) == 3 && bad == 0 && memcmp(b, b2, sizeof b) == 0);
    CHECK(keymap_format(b, out, 8) == -1);

    if (fails) {
        printf("keymap_test: %d failed\n", fails);
        return 1;
    }
    printf("keymap_test: all passed\n");
    return 0;
}
