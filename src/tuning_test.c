/* tuning_test.c - unit tests for the tuning range model (src/tuning.c).
 * Build: gcc -std=c11 -Wall -Wextra -Werror -I src -o .ai/tuning_test.exe src/tuning_test.c src/tuning.c && .ai/tuning_test.exe */
#include "tuning.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_fail;
static void expect(const char *what, int ok)
{
    printf("%s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok)
        s_fail = 1;
}
static int near(float a, float b) { return fabsf(a - b) < 1e-3f; }

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)calloc(1, (size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {}
    fclose(f);
    return b;
}

int main(void)
{
    int life, maxl, speed, have;
    char dir[64] = ".ai/tuning_t", path[128], log[128];
    char *s;

    tuning_reset();
    maxl = tuning_find("player.max_life");
    life = tuning_find("player.life");
    speed = tuning_find("player.move_speed");
    expect("rows exist", maxl >= 0 && life >= 0 && speed >= 0 && tuning_count() == 5);
    expect("unknown key", tuning_find("nope") < 0);

    tuning_observe(maxl, 170.0f);
    expect("default from first observe", tuning_row(maxl)->have_def && near(tuning_row(maxl)->def, 170.0f));
    tuning_observe(maxl, 180.0f);
    expect("default stays, current follows", near(tuning_row(maxl)->def, 170.0f) && near(tuning_row(maxl)->cur, 180.0f));
    expect("blank range = x0.25..x4", near(tuning_row(maxl)->min, 42.5f) && near(tuning_row(maxl)->max, 680.0f));
    expect("clamped to max", near(tuning_set(maxl, 5000.0f), 680.0f));
    expect("clamped to min", near(tuning_set(maxl, 1.0f), 42.5f));
    tuning_set(maxl, 170.0f);
    expect("nudge", near(tuning_nudge(maxl, 2), 190.0f) && near(tuning_nudge(maxl, -2), 170.0f));
    expect("no ideal = yellow", tuning_colour(maxl) == TUNE_YELLOW);
    tuning_row(maxl)->ideal = 200.0f;
    tuning_row(maxl)->have_ideal = 1;
    tuning_set(maxl, 195.0f);
    expect("within 5% = green", tuning_colour(maxl) == TUNE_GREEN);
    tuning_set(maxl, 170.0f);
    expect("off ideal = yellow", tuning_colour(maxl) == TUNE_YELLOW);
    tuning_row(maxl)->cur = 900.0f;
    expect("out of range = red", tuning_colour(maxl) == TUNE_RED);
    expect("never seen = grey", tuning_colour(tuning_find("player.stamina")) == TUNE_GREY);
    tuning_set(maxl, 170.0f);

    /* files */
    system("mkdir .ai 2>nul");
    system("mkdir .ai\\tuning_t 2>nul");
    remove(".ai/tuning_t/tuning_final.json");
    remove(".ai/tuning_t/changelog.md");
    snprintf(path, sizeof path, "%s/ranges.json", dir);
    expect("save ranges", tuning_save_ranges(path) == 1);
    tuning_row(maxl)->ideal = 0.0f;
    tuning_row(maxl)->have_ideal = 0;
    tuning_row(maxl)->min = tuning_row(maxl)->max = 0.0f;
    expect("load ranges", tuning_load_ranges(path) == 1);
    expect("ranges round trip", near(tuning_row(maxl)->min, 42.5f) && near(tuning_row(maxl)->max, 680.0f)
                                && tuning_row(maxl)->have_ideal && near(tuning_row(maxl)->ideal, 200.0f));
    expect("missing file", tuning_load_ranges(".ai/tuning_t/none.json") == 0);

    tuning_observe(speed, 1.0f);
    tuning_observe(life, 120.0f);
    tuning_set(speed, 1.5f);
    tuning_set(maxl, 250.0f);
    snprintf(log, sizeof log, "%s/changelog.md", dir);
    expect("finalize writes 2 changed values", tuning_finalize(dir, log, "2026-11-11 22:10", "first try") == 2);
    s = slurp(".ai/tuning_t/tuning_final.json");
    expect("final has the changed values, not the life state",
           s && strstr(s, "\"player.max_life\": 250") && strstr(s, "\"player.move_speed\": 1.5") && !strstr(s, "player.life\""));
    free(s);
    s = slurp(log);
    expect("changelog line with note and old -> new", s && strstr(s, "first try") && strstr(s, "170 -> 250"));
    free(s);
    expect("finalized now shown", tuning_row(maxl)->have_fin && near(tuning_row(maxl)->fin, 250.0f));
    tuning_set(maxl, 275.0f);
    expect("delta vs finalized", near(tuning_delta_pct(maxl, &have), 10.0f) && have);

    expect("second finalize", tuning_finalize(dir, log, "2026-11-11 22:20", "second") == 2);
    s = slurp(".ai/tuning_t/history/tuning_final_2026-11-11_2220.json");
    expect("old final kept in history", s && strstr(s, "250"));
    free(s);

    tuning_row(maxl)->have_fin = 0;
    expect("load final", tuning_load_final(".ai/tuning_t/tuning_final.json") == 1
                         && tuning_row(maxl)->have_fin && near(tuning_row(maxl)->fin, 275.0f));

    snprintf(path, sizeof path, "%s/preset.json", dir);
    expect("save preset", tuning_save_values(path) == 1);
    tuning_set(maxl, 100.0f);
    expect("load preset", tuning_load_preset(path) == 1 && near(tuning_row(maxl)->cur, 275.0f));
    expect("preset does not set life (state)", near(tuning_row(life)->cur, 120.0f));
    snprintf(path, sizeof path, "%s/snapshot.md", dir);
    expect("snapshot", tuning_snapshot(path, "2026-11-11") == 1);
    s = slurp(path);
    expect("snapshot lists rows", s && strstr(s, "| Max life | player.max_life | 275 | 170 |"));
    free(s);
    puts(s_fail ? "FAILED" : "all passed");
    return s_fail;
}
