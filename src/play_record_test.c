/*
 * play_record_test.c - unit test for src/play_record.c (area messages, JSON strings, session names, frame stats).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o play_record_test src/play_record_test.c src/play_record.c
 */
#include "play_record.h"

#include <math.h>
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

int main(void)
{
    char a[64], s[64], j[64];
    struct pr_frames f;

    expect("story section", pr_area_from_msg("AREA_NAME_SET area/hon_under/hon_under_B_out, 1", a, sizeof a, s, sizeof s)
           && !strcmp(a, "hon_under") && !strcmp(s, "hon_under_B_out"));
    expect("map viewer", pr_area_from_msg("area_load area/bettou/bettou_C start", a, sizeof a, s, sizeof s)
           && !strcmp(a, "bettou") && !strcmp(s, "bettou_C"));
    expect("area only", pr_area_from_msg("AREA DATA SET  area/gesui", a, sizeof a, s, sizeof s)
           && !strcmp(a, "gesui") && s[0] == 0);
    expect("not an area", !pr_area_from_msg("makino: UserSaveData Reading... GAME 5", a, sizeof a, s, sizeof s));
    expect("empty name", !pr_area_from_msg("AREA_NAME_SET area/, 1", a, sizeof a, s, sizeof s));
    expect("short buffer", pr_area_from_msg("AREA_NAME_SET area/hon_under/x", a, 4, s, sizeof s) && !strcmp(a, "hon"));

    expect("title 7F", !strcmp(pr_area_title("hon_under"), "7F / Main Research Center"));
    expect("title unknown", pr_area_title("park3") == NULL);

    pr_json_str("a\"b\\c\nd\x01", j, sizeof j);
    expect("json escape", !strcmp(j, "a\\\"b\\\\c\\nd\\u0001"));
    pr_json_str("abcdef", j, 4);
    expect("json truncates", !strcmp(j, "abc"));

    pr_session_name(2026, 11, 14, 20, 5, 9, j, sizeof j);
    expect("session name", !strcmp(j, "20261114-200509"));

    memset(&f, 0, sizeof f);
    expect("no frames fps 0", pr_frames_fps(&f) == 0.0);
    pr_frames_add(&f, 20.0);
    pr_frames_add(&f, 20.0);
    pr_frames_add(&f, 60.0);
    pr_frames_add(&f, 150.0);
    expect("frame counts", f.n == 4 && f.over50 == 2 && f.over100 == 1 && f.worst_ms == 150.0);
    expect("fps", fabs(pr_frames_fps(&f) - 4000.0 / 250.0) < 1e-9);

    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    printf("all ok\n");
    return 0;
}
