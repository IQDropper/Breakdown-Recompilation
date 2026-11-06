/*
 * video_menu_policy_test.c
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -I src -o video_menu_policy_test \
 *       src/video_menu_policy_test.c src/video_menu_policy.c
 */
#include "video_menu_policy.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

static void expect(const char *name, int cond)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", name);
        g_fail++;
    } else {
        printf("ok %s\n", name);
    }
}

static void test_pause_action(void)
{
    expect("start bit is XINPUT start", VIDEO_PAD_START == 0x0010u);

    expect("rising start opens",
           video_menu_pause_action(VIDEO_PAD_START, 0, 0) == VIDEO_PAUSE_OPEN);
    expect("start plus other buttons still opens",
           video_menu_pause_action(VIDEO_PAD_START | VIDEO_PAD_A, 0, 0) == VIDEO_PAUSE_OPEN);
    expect("held start does not repeat",
           video_menu_pause_action(VIDEO_PAD_START, VIDEO_PAD_START, 0) == VIDEO_PAUSE_NONE);
    expect("start release is not an open",
           video_menu_pause_action(0, VIDEO_PAD_START, 0) == VIDEO_PAUSE_NONE);
    expect("dpad is not start",
           video_menu_pause_action(VIDEO_PAD_DPAD_DOWN, 0, 0) == VIDEO_PAUSE_NONE);
    expect("A is not start",
           video_menu_pause_action(VIDEO_PAD_A, 0, 0) == VIDEO_PAUSE_NONE);
    expect("start while menu open does not open again",
           video_menu_pause_action(VIDEO_PAD_START, 0, 1) == VIDEO_PAUSE_NONE);
    expect("start while menu open does not close it",
           video_menu_pause_action(VIDEO_PAD_START, 0, 1) == VIDEO_PAUSE_NONE);
}

static void test_filter(void)
{
    unsigned held;

    expect("closed menu passes start",
           video_menu_filter_buttons(VIDEO_PAD_START, 0) == VIDEO_PAD_START);
    expect("closed menu passes dpad",
           video_menu_filter_buttons(VIDEO_PAD_DPAD_LEFT | VIDEO_PAD_A, 0)
           == (VIDEO_PAD_DPAD_LEFT | VIDEO_PAD_A));

    held = video_menu_filter_buttons(
        VIDEO_PAD_START | VIDEO_PAD_DPAD_DOWN | VIDEO_PAD_DPAD_LEFT | VIDEO_PAD_A | VIDEO_PAD_BACK,
        1);
    expect("open menu drops start", (held & VIDEO_PAD_START) == 0);
    expect("open menu drops dpad",
           (held & (VIDEO_PAD_DPAD_DOWN | VIDEO_PAD_DPAD_LEFT)) == 0);
    expect("open menu keeps A and back",
           (held & VIDEO_PAD_A) != 0 && (held & VIDEO_PAD_BACK) != 0);
}

/* The press that pauses the game is delivered. Later polls, once Video is
 * open, keep Start and the d-pad in the host window. */
static void test_pause_then_hold(void)
{
    unsigned now = VIDEO_PAD_START;
    unsigned prev = 0;
    int open = 0;
    int action = video_menu_pause_action(now, prev, open);
    unsigned passed = video_menu_filter_buttons(now, open);

    expect("pause press asks to open", action == VIDEO_PAUSE_OPEN);
    expect("pause press still reaches the game", (passed & VIDEO_PAD_START) != 0);

    open = 1;
    prev = now;
    now = VIDEO_PAD_START | VIDEO_PAD_DPAD_RIGHT;
    action = video_menu_pause_action(now, prev, open);
    passed = video_menu_filter_buttons(now, open);
    expect("held start after open is not another open", action == VIDEO_PAUSE_NONE);
    expect("held start after open does not reach the game", (passed & VIDEO_PAD_START) == 0);
    expect("right after open does not reach the game", (passed & VIDEO_PAD_DPAD_RIGHT) == 0);
}

/* MSVC and mingw reject two parameters with the same name. client_matches
 * used HWND h and unsigned h. */
static void test_client_matches_params(void)
{
    FILE *f = fopen("src/video_menu.c", "r");
    char line[512];
    int bad = 0;
    int saw = 0;

    expect("video_menu.c readable", f != NULL);
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, "client_matches"))
            continue;
        saw = 1;
        /* "HWND h" is a prefix of "HWND hwnd", so match the old parameter
         * list itself. MSVC rejects (HWND h, ..., unsigned h). */
        if (strstr(line, "(HWND h,") || strstr(line, ", unsigned h)"))
            bad = 1;
    }
    fclose(f);
    expect("client_matches is declared", saw == 1);
    expect("client_matches parameters are unique", bad == 0);
}

int main(void)
{
    test_pause_action();
    test_filter();
    test_pause_then_hold();
    test_client_matches_params();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    puts("all passed");
    return 0;
}
