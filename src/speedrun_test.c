/*
 * speedrun_test.c - unit test for the speedrun timer model (src/speedrun.c).
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o speedrun_test src/speedrun_test.c src/speedrun.c
 */
#include "speedrun.h"

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

#define PAK(n) "\\Device\\Harddisk0\\Partition5\\area\\" n "\\" n "_A.pak"

static void test_area_pak(void)
{
    char a[SPEEDRUN_NAME_LEN];
    expect("pak: hard disk path", speedrun_area_pak(PAK("hon_under"), a, sizeof a) && !strcmp(a, "hon_under_A"));
    expect("pak: Z: path, other case", speedrun_area_pak("Z:\\AREA\\Hon_Under\\hon_under_b.PAK", a, sizeof a)
                                           && !strcmp(a, "hon_under_B"));
    expect("pak: forward slashes", speedrun_area_pak("d:/area/st01/st01_A.pak", a, sizeof a) && !strcmp(a, "st01_A"));
    expect("pak: block file is not one",
           !speedrun_area_pak("\\Device\\Harddisk0\\Partition5\\area\\hon_under\\block_A_s02a02_130_p.pak", a, sizeof a));
    expect("pak: <name>.pak is not one", !speedrun_area_pak("D:\\area\\hon_under\\hon_under.pak", a, sizeof a));
    expect("pak: .nut is not one", !speedrun_area_pak("D:\\area\\hon_under\\hon_under_A.nut", a, sizeof a));
    expect("pak: other folder name", !speedrun_area_pak("D:\\area\\hon_under\\other_A.pak", a, sizeof a));
    expect("pak: no area folder", !speedrun_area_pak("D:\\media\\hon_under\\hon_under_A.pak", a, sizeof a));
    expect("pak: deeper path", !speedrun_area_pak("D:\\area\\x\\y\\x_A.pak", a, sizeof a));
    expect("pak: two-letter part", !speedrun_area_pak("D:\\area\\x\\x_AB.pak", a, sizeof a));
    expect("pak: small buffer", !speedrun_area_pak(PAK("hon_under"), a, 5));
    expect("pak: NULL", !speedrun_area_pak(NULL, a, sizeof a));
    expect("area path: yes", speedrun_area_path("\\Device\\CdRom0\\romdata\\area\\hon_under\\hon_under_A_out.evd"));
    expect("area path: area_x is not area", !speedrun_area_path("D:\\area_x\\a.pak"));
    expect("area path: no", !speedrun_area_path("D:\\media\\title.xmv"));
}

static void test_format(void)
{
    char b[32];
    speedrun_format(0, b, sizeof b);
    expect("format 0", !strcmp(b, "0:00.00"));
    speedrun_format(61234, b, sizeof b);
    expect("format 1:01.23", !strcmp(b, "1:01.23"));
    speedrun_format(3600000 + 2 * 60000 + 3456, b, sizeof b);
    expect("format 1:02:03.45", !strcmp(b, "1:02:03.45"));
    speedrun_format(9999, b, sizeof b);
    expect("format rounds down", !strcmp(b, "0:09.99"));
}

static void test_ready_until_pak(void)
{
    struct speedrun s;
    speedrun_init(&s);
    speedrun_update(&s, 1000);
    expect("ready: no time", speedrun_igt(&s, 5000) == 0 && s.state == SPEEDRUN_READY);
    expect("ready: other area files do not start",
           speedrun_on_path(&s, "D:\\area\\hon_under\\hon_under.pak", 6000) == SPEEDRUN_EV_NONE
               && s.state == SPEEDRUN_READY);
    expect("ready: non-area files ignored", speedrun_on_path(&s, "D:\\media\\a.xmv", 6000) == SPEEDRUN_EV_NONE);
    expect("start on first area pak", speedrun_on_path(&s, PAK("st01"), 10000) == SPEEDRUN_EV_START
                                          && s.state == SPEEDRUN_RUNNING && s.nsplits == 1
                                          && !strcmp(s.splits[0].area, "st01_A") && s.splits[0].igt_ms == 0);
    expect("start: loading right away", speedrun_loading(&s, 10100));
    expect("start: load does not count", speedrun_igt(&s, 10400) == 0);
    /* load ends 500 ms after the pak (no more area files) */
    expect("load over at quiet time", !speedrun_loading(&s, 10500));
    expect("igt counts after load", speedrun_igt(&s, 11500) == 1000);
    expect("load ms recorded", s.load_ms == 500);
    expect("rta from start", speedrun_rta(&s, 11500) == 1500);
}

static void test_load_extends_and_caps(void)
{
    struct speedrun s;
    uint64_t t;
    speedrun_init(&s);
    speedrun_on_path(&s, PAK("st01"), 0);
    speedrun_on_path(&s, "D:\\area\\st01\\st01_A.nut", 400);
    speedrun_on_path(&s, "D:\\area\\st01\\st01_A.evd", 800);
    expect("load extended by area files", speedrun_loading(&s, 1200) && speedrun_igt(&s, 1200) == 0);
    expect("load ends 500 ms after the last area file", speedrun_igt(&s, 2300) == 1000);
    expect("non-area file does not start a load", speedrun_on_path(&s, "D:\\media\\a.xmv", 2400) == SPEEDRUN_EV_NONE
                                                     && !speedrun_loading(&s, 2400));
    expect("area file alone does not start a load",
           speedrun_on_path(&s, "D:\\area\\st01\\st01_A_out.evd", 2500) == SPEEDRUN_EV_NONE
               && !speedrun_loading(&s, 2600));

    /* a file the game keeps re-opening: the load stops at the 30 s cap */
    speedrun_init(&s);
    speedrun_on_path(&s, PAK("st01"), 0);
    for (t = 100; t < 60000; t += 100)
        speedrun_on_path(&s, "D:\\area\\st01\\st01_A_out.evd", t);
    expect("load capped", !speedrun_loading(&s, 60000) && speedrun_igt(&s, 60000) == 60000 - SPEEDRUN_LOAD_MAX_MS);
    expect("capped load ms", s.load_ms == SPEEDRUN_LOAD_MAX_MS);
}

static void test_pause(void)
{
    struct speedrun s;
    speedrun_init(&s);
    speedrun_on_path(&s, PAK("st01"), 0);
    speedrun_update(&s, 1500);            /* igt 1000 */
    speedrun_set_paused(&s, 1, 2000);     /* igt 1500 */
    expect("paused: frozen", speedrun_igt(&s, 9000) == 1500);
    speedrun_set_paused(&s, 0, 10000);
    expect("unpaused: counts again", speedrun_igt(&s, 10250) == 1750);
    expect("pause ms", s.pause_ms == 8000);

    /* pause during a load: load time first, then pause */
    speedrun_init(&s);
    speedrun_set_paused(&s, 1, 0);
    speedrun_on_path(&s, PAK("st01"), 100);
    expect("paused through a load: nothing counts", speedrun_igt(&s, 5000) == 0 && s.load_ms == 500);
    speedrun_set_paused(&s, 0, 6000);
    expect("then counts", speedrun_igt(&s, 7000) == 1000);

    /* reset keeps the game's pause flag */
    speedrun_set_paused(&s, 1, 8000);
    speedrun_reset(&s);
    expect("reset: ready, still paused", s.state == SPEEDRUN_READY && s.paused && s.igt_ms == 0 && s.nsplits == 0);
}

static void test_splits(void)
{
    struct speedrun s;
    char row[160];
    speedrun_init(&s);
    speedrun_on_path(&s, PAK("st01"), 0);
    speedrun_on_path(&s, PAK("st01"), 50);              /* the same pak again (it opens a few times) */
    expect("same pak: no split", s.nsplits == 1);
    speedrun_update(&s, 10550);                        /* igt 10000 */
    expect("new area: split", speedrun_on_path(&s, PAK("st02"), 10550) == SPEEDRUN_EV_SPLIT && s.nsplits == 2);
    expect("split igt", s.splits[1].igt_ms == 10000 && s.splits[1].rta_ms == 10550 && s.splits[1].load_ms == 550);
    speedrun_update(&s, 21050);                        /* +10000 after the 500 ms load */
    expect("second part of same area: split",
           speedrun_on_path(&s, "Z:\\area\\st02\\st02_B.pak", 21050) == SPEEDRUN_EV_SPLIT && s.nsplits == 3);
    speedrun_update(&s, 31550);
    expect("back to an old area: split", speedrun_on_path(&s, PAK("st01"), 31550) == SPEEDRUN_EV_SPLIT);
    expect("death reload of same area: no split", speedrun_on_path(&s, PAK("st01"), 41550) == SPEEDRUN_EV_NONE
                                                      && s.nsplits == 4);

    speedrun_csv_row(&s, 0, "r1", row, sizeof row);
    expect("csv row 0", !strcmp(row, "r1,0,st01_A,0,0:00.00,0,0,0"));
    speedrun_csv_row(&s, 1, "r1", row, sizeof row);
    expect("csv row 1", !strcmp(row, "r1,1,st02_A,10000,0:10.00,10000,10550,550"));
    speedrun_csv_row(&s, 2, "r1", row, sizeof row);
    expect("csv row 2", !strcmp(row, "r1,2,st02_B,20000,0:20.00,10000,21050,1050"));
    speedrun_csv_row(&s, 9, "r1", row, sizeof row);
    expect("csv row out of range", row[0] == 0);
    speedrun_csv_end(&s, 50000, "r1", row, sizeof row);
    /* igt: 30000 at the st01 split, +9500 to the reload at 41550, then the reload re-opened the pak
     * while not loading -> a new 500 ms load, then +7950 */
    expect("csv end row", !strcmp(row, "r1,4,end,47450,0:47.45,17450,50000,2550"));

    speedrun_reset(&s);
    speedrun_csv_end(&s, 60000, "r1", row, sizeof row);
    expect("csv end when ready: empty", row[0] == 0);
}

static void test_split_limit(void)
{
    struct speedrun s;
    char path[96];
    int i;
    speedrun_init(&s);
    for (i = 0; i < SPEEDRUN_MAX_SPLITS + 5; i++) {
        snprintf(path, sizeof path, "D:\\area\\a%d\\a%d_A.pak", i, i);
        speedrun_on_path(&s, path, (uint64_t)i * 1000);
    }
    expect("splits stop at the limit", s.nsplits == SPEEDRUN_MAX_SPLITS);
}

static void test_clock_back(void)
{
    struct speedrun s;
    speedrun_init(&s);
    speedrun_on_path(&s, PAK("st01"), 1000);
    speedrun_update(&s, 3000);
    expect("clock going back is ignored", speedrun_igt(&s, 2000) == 1500 && speedrun_igt(&s, 3000) == 1500);
}

int main(void)
{
    test_area_pak();
    test_format();
    test_ready_until_pak();
    test_load_extends_and_caps();
    test_pause();
    test_splits();
    test_split_limit();
    test_clock_back();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    printf("all speedrun tests passed\n");
    return 0;
}
