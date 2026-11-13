/*
 * gcc -std=c11 -Wall -Wextra -Werror -I src -o pc_options_test \
 *     src/pc_options_test.c src/pc_options.c src/video_config.c src/display_host.c
 */
#include "pc_options.h"
#include "a11y_input.h"

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

static void pulse(unsigned buttons)
{
    pc_options_on_port(0, buttons);
    pc_options_on_port(0, 0);
}

static struct video_settings base_settings(void)
{
    struct video_settings s;
    video_settings_stock(&s);
    s.render_scale = 2;
    s.window_scale = 2;
    s.aspect = video_settings_aspect_index("16:9");
    s.filter = 2;
    s.borderless = 0;
    return s;
}

static void test_start_alone(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    r = pc_options_on_port(0, PC_PAD_START);
    expect("start alone stays closed", pc_options_is_open() == 0);
    expect("start alone reaches the guest", (r.guest_buttons & PC_PAD_START) != 0);
    expect("start alone is not a page action", r.action == PC_ACT_NONE);
    expect("start alone does not swallow motion", r.suppress_motion == 0);
}

static void test_chord(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    r = pc_options_on_port(0, PC_PAD_BACK);
    expect("back alone stays closed", pc_options_is_open() == 0);
    expect("back alone reaches the guest", (r.guest_buttons & PC_PAD_BACK) != 0);

    r = pc_options_on_port(0, PC_PAD_BACK | PC_PAD_START);
    expect("start while back is held opens", r.action == PC_ACT_OPENED && pc_options_is_open());
    expect("chord swallows start and back",
           (r.guest_buttons & (PC_PAD_START | PC_PAD_BACK)) == 0);
    expect("chord swallows motion", r.suppress_motion == 1);
    expect("open focuses the first row", pc_options_row() == 0);
    expect("draft starts at the baseline",
           pc_options_draft() && pc_options_draft()->render_scale == 2);

    r = pc_options_on_port(0, PC_PAD_BACK | PC_PAD_START);
    expect("held chord does not close", pc_options_is_open() == 1 && r.action == PC_ACT_NONE);
}

static void test_same_poll_chord(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    r = pc_options_on_port(0, PC_PAD_BACK | PC_PAD_START);
    expect("both rising opens", r.action == PC_ACT_OPENED);
    expect("title start is not forwarded", (r.guest_buttons & PC_PAD_START) == 0);
}

static void test_nav_filter_repeat_and_dedup(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    int i;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_on_port(0, PC_PAD_BACK | PC_PAD_START);
    r = pc_options_on_port(0, 0);
    expect("release after open stays open", pc_options_is_open() == 1);

    r = pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("down moves once", r.action == PC_ACT_NONE && pc_options_row() == 1);
    expect("down is not forwarded", (r.guest_buttons & PC_PAD_DPAD_DOWN) == 0);
    expect("open page swallows motion", r.suppress_motion == 1);

    r = pc_options_set_ui_bit(PC_PAD_DPAD_DOWN, 1);
    expect("keyboard echo does not move again", pc_options_row() == 1);

    for (i = 0; i < 7; i++)
        pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("hold has not repeated yet", pc_options_row() == 1);
    pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("hold repeats on the 8th extra poll", pc_options_row() == 2);
    pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("three polls do not repeat again", pc_options_row() == 2);
    pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("then every 4 polls", pc_options_row() == 3);
}

static void test_apply_reset_revert_and_lock(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    char buf[64];
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(PC_LOCK_SCALE);
    pc_options_on_f10();
    expect("f10 opens", pc_options_is_open() == 1);

    pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("locked scale does not nudge", pc_options_draft()->render_scale == 2);
    pc_options_row_value(PC_ROW_SCALE, buf, sizeof buf);
    expect("lock is visible", strstr(buf, "launch script") != NULL);

    /* Window size is the next row and is not locked. */
    pulse(0);
    pulse(PC_PAD_DPAD_DOWN);
    expect("row is window size", pc_options_row() == PC_ROW_WINDOW);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("window size previews",
           r.action == PC_ACT_PREVIEW && pc_options_draft()->window_scale == 3);
    expect("committed stays at baseline until apply",
           pc_options_committed()->window_scale == 2);

    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    r = pc_options_on_port(0, PC_PAD_A);
    expect("reset previews stock except the lock",
           r.action == PC_ACT_PREVIEW
           && pc_options_draft()->render_scale == 2
           && pc_options_draft()->window_scale == 1
           && pc_options_draft()->aspect == 0
           && pc_options_draft()->filter == 0
           && pc_options_draft()->borderless == 0);
    expect("reset does not commit", pc_options_committed()->window_scale == 2);

    r = pc_options_on_esc();
    expect("esc reverts and closes",
           r.action == PC_ACT_REVERT && pc_options_is_open() == 0);
    expect("draft is the committed baseline again",
           pc_options_draft()->window_scale == 2 && pc_options_draft()->render_scale == 2);
}

static void test_save_failure(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    pc_options_on_f10();
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("scale previews", r.action == PC_ACT_PREVIEW && pc_options_draft()->render_scale == 3);
    while (pc_options_row() != PC_ROW_APPLY)
        pulse(PC_PAD_DPAD_DOWN);
    r = pc_options_on_port(0, PC_PAD_A);
    expect("apply asks to save", r.action == PC_ACT_SAVE && pc_options_is_open() == 1);
    pc_options_save_result(0);
    expect("failed save keeps the old commit", pc_options_committed()->render_scale == 2);
    expect("failed save keeps the preview", pc_options_draft()->render_scale == 3);
    expect("failure is visible", strstr(pc_options_status(), "unchanged") != NULL);
    pc_options_save_result(1);
    expect("a later success commits", pc_options_committed()->render_scale == 3);
}

static void test_back_while_start_held_stays(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_on_port(0, PC_PAD_START | PC_PAD_BACK);
    r = pc_options_on_port(0, PC_PAD_START);
    expect("releasing back while start is held stays open", pc_options_is_open() == 1);
    r = pc_options_on_port(0, PC_PAD_B);
    expect("b reverts", r.action == PC_ACT_REVERT && pc_options_is_open() == 0);
}

static void test_second_port_does_not_drop_chord(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_on_port(1, 0);
    r = pc_options_on_port(0, PC_PAD_START | PC_PAD_BACK);
    expect("port 0 chord still opens", r.action == PC_ACT_OPENED);
    r = pc_options_on_port(1, 0);
    expect("idle port does not close", pc_options_is_open() == 1);
}

static void test_layout(void)
{
    struct pc_rect frame, panel;
    pc_options_frame_rect(1707, 960, &frame);
    pc_options_panel_rect(1707, 960, &panel);
    expect("16:9 frame is 4:3 high", frame.h == 960 && frame.w == 1280);
    expect("16:9 frame is centered", frame.x == (1707 - 1280) / 2 && frame.y == 0);
    expect("panel is inside the frame",
           panel.x >= frame.x && panel.y >= frame.y
           && panel.x + panel.w <= frame.x + frame.w
           && panel.y + panel.h <= frame.y + frame.h);
    expect("panel does not cover the side bars", panel.x > 0 && panel.x + panel.w < 1707);

    pc_options_frame_rect(640, 480, &frame);
    pc_options_panel_rect(640, 480, &panel);
    expect("4:3 frame fills the client", frame.x == 0 && frame.y == 0 && frame.w == 640 && frame.h == 480);
    expect("4:3 panel is inset",
           panel.x > 0 && panel.y > 0 && panel.x + panel.w < 640 && panel.y + panel.h < 480);

    /* Borderless client is the monitor. 1920x1080 is wider than 4:3. */
    pc_options_frame_rect(1920, 1080, &frame);
    pc_options_panel_rect(1920, 1080, &panel);
    expect("borderless frame is 4:3", frame.w == 1440 && frame.h == 1080 && frame.y == 0);
    expect("borderless frame is centered", frame.x == (1920 - 1440) / 2);
    expect("borderless panel stays inside the frame",
           panel.x >= frame.x && panel.y >= frame.y
           && panel.x + panel.w <= frame.x + frame.w
           && panel.y + panel.h <= frame.y + frame.h);
    expect("borderless panel misses the side bars",
           panel.x > frame.x && panel.x + panel.w < frame.x + frame.w);

    /* A taller client letterboxes above and below the 4:3 picture. */
    pc_options_frame_rect(800, 900, &frame);
    pc_options_panel_rect(800, 900, &panel);
    expect("tall frame is 4:3 wide", frame.w == 800 && frame.h == 600 && frame.x == 0);
    expect("tall frame is centered", frame.y == (900 - 600) / 2);
    expect("tall panel stays inside the frame",
           panel.x >= frame.x && panel.y >= frame.y
           && panel.x + panel.w <= frame.x + frame.w
           && panel.y + panel.h <= frame.y + frame.h);
}

static void test_port_zero_drops_stuck_start(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_on_port(1, PC_PAD_START);
    r = pc_options_on_port(0, PC_PAD_BACK);
    expect("stuck start on another port still forms the chord", r.action == PC_ACT_OPENED);
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_on_port(1, PC_PAD_START);
    pc_options_on_port(1, 0);
    r = pc_options_on_port(0, PC_PAD_BACK);
    expect("a port that reports 0 no longer supplies start",
           r.action == PC_ACT_NONE && pc_options_is_open() == 0);
    expect("back still reaches the guest", (r.guest_buttons & PC_PAD_BACK) != 0);
}

static void test_release_ui_drops_stuck_start(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_ui_bit(PC_PAD_START, 1);
    pc_options_release_ui();
    r = pc_options_set_ui_bit(PC_PAD_BACK, 1);
    expect("released start does not complete a later chord",
           r.action == PC_ACT_NONE && pc_options_is_open() == 0);
}

static void test_keyboard_only_moves_the_page(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_ui_bit(PC_PAD_BACK, 1);
    r = pc_options_set_ui_bit(PC_PAD_START, 1);
    expect("keyboard chord opens with no pad port", r.action == PC_ACT_OPENED);
    pc_options_set_ui_bit(PC_PAD_START, 0);
    pc_options_set_ui_bit(PC_PAD_BACK, 0);
    r = pc_options_set_ui_bit(PC_PAD_DPAD_DOWN, 1);
    expect("keyboard down moves a row", pc_options_row() == 1);
    pc_options_release_ui();
    r = pc_options_on_port(0, PC_PAD_DPAD_DOWN);
    expect("after release the same down is a new edge", pc_options_row() == 2);
}

static void test_rows_are_real(void)
{
    int i;
    expect("thirteen rows", PC_ROW_COUNT == 13);
    for (i = 0; i < PC_ROW_COUNT; i++) {
        const char *t = pc_options_row_title(i);
        expect("row has a title", t && t[0]);
        expect("no row is titled Exclusive (it is a value of Window mode)", !t || strstr(t, "Exclusive") == NULL);
        expect("no hor+ row", !t || strstr(t, "Hor+") == NULL);
    }
    expect("page name is not Xbox Options",
           strstr(pc_options_row_title(PC_ROW_APPLY), "Xbox") == NULL);
}

static void test_window_mode_row_has_exclusive(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    char buf[64];
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    pc_options_on_f10();
    while (pc_options_row() != PC_ROW_MODE)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_MODE, buf, sizeof buf);
    expect("mode starts windowed", strcmp(buf, "Windowed") == 0);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("right: borderless",
           r.action == PC_ACT_PREVIEW && pc_options_draft()->borderless == 1);
    pulse(0);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    pc_options_row_value(PC_ROW_MODE, buf, sizeof buf);
    expect("right again: exclusive",
           r.action == PC_ACT_PREVIEW && pc_options_draft()->borderless == 2
           && strcmp(buf, "Exclusive") == 0);
    expect("committed is still windowed", pc_options_committed()->borderless == 0);
    pulse(0);
    r = pc_options_on_port(0, PC_PAD_DPAD_LEFT);
    expect("left: back to borderless", pc_options_draft()->borderless == 1);
    pulse(0);
    pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    r = pc_options_on_port(0, PC_PAD_A);
    expect("reset leaves exclusive", pc_options_draft()->borderless == 0);
    pc_options_on_esc();
}

static void test_vsync_row(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    char buf[64];
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    pc_options_on_f10();
    expect("vsync row is named", strcmp(pc_options_row_title(PC_ROW_VSYNC), "VSync") == 0);
    while (pc_options_row() != PC_ROW_VSYNC)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_VSYNC, buf, sizeof buf);
    expect("vsync starts off", strcmp(buf, "Off") == 0 && pc_options_draft()->vsync == 0);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    pc_options_row_value(PC_ROW_VSYNC, buf, sizeof buf);
    expect("right turns vsync on", r.action == PC_ACT_PREVIEW && pc_options_draft()->vsync == 1 && strcmp(buf, "On") == 0);
    pulse(0);
    pulse(PC_PAD_DPAD_DOWN);
    expect("hold to repeat follows vsync", pc_options_row() == PC_ROW_REPEAT);
    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_A);
    expect("reset turns vsync off", pc_options_draft()->vsync == 0);
    pc_options_on_esc();

    pc_options_reset();
    s.vsync = 1;
    pc_options_set_baseline(&s);
    pc_options_set_locks(PC_LOCK_VSYNC);
    pc_options_on_f10();
    while (pc_options_row() != PC_ROW_VSYNC)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_VSYNC, buf, sizeof buf);
    expect("locked vsync says launch script", strstr(buf, "launch script") != NULL);
    pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("locked vsync does not change", pc_options_draft()->vsync == 1);
    pc_options_on_esc();
}

static void test_a11y_rows(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    char buf[64];
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    pc_options_on_f10();
    expect("repeat row is named", strcmp(pc_options_row_title(PC_ROW_REPEAT), "Hold to repeat") == 0);
    expect("toggle row is named", strcmp(pc_options_row_title(PC_ROW_TOGGLE), "Hold toggle") == 0);
    expect("video rows match page rows", PC_ROW_REPEAT == VIDEO_ROW_REPEAT && PC_ROW_TOGGLE == VIDEO_ROW_TOGGLE);
    expect("repeat row has help text", strstr(video_row_detail(pc_options_draft(), PC_ROW_REPEAT), "10 presses") != NULL);
    expect("toggle row has help text", strstr(video_row_detail(pc_options_draft(), PC_ROW_TOGGLE), "again") != NULL);
    while (pc_options_row() != PC_ROW_REPEAT)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_REPEAT, buf, sizeof buf);
    expect("repeat starts off", strcmp(buf, "Off") == 0 && pc_options_draft()->a11y_repeat == A11Y_REPEAT_OFF);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    pc_options_row_value(PC_ROW_REPEAT, buf, sizeof buf);
    expect("right picks A", r.action == PC_ACT_PREVIEW && pc_options_draft()->a11y_repeat == A11Y_REPEAT_A
                                && strcmp(buf, "A") == 0);
    pulse(0);
    pulse(PC_PAD_DPAD_LEFT);
    pulse(PC_PAD_DPAD_LEFT);
    pc_options_row_value(PC_ROW_REPEAT, buf, sizeof buf);
    expect("left wraps to A B X Y", pc_options_draft()->a11y_repeat == A11Y_REPEAT_FACE && strcmp(buf, "A B X Y") == 0);
    pulse(PC_PAD_DPAD_DOWN);
    expect("toggle follows repeat", pc_options_row() == PC_ROW_TOGGLE);
    pc_options_row_value(PC_ROW_TOGGLE, buf, sizeof buf);
    expect("toggle starts off", strcmp(buf, "Off") == 0 && pc_options_draft()->a11y_toggle == A11Y_TOGGLE_OFF);
    pulse(PC_PAD_DPAD_RIGHT);
    pc_options_row_value(PC_ROW_TOGGLE, buf, sizeof buf);
    expect("right picks right trigger", pc_options_draft()->a11y_toggle == A11Y_TOGGLE_RT && strcmp(buf, "Right trigger") == 0);
    pulse(PC_PAD_DPAD_DOWN);
    expect("apply follows toggle", pc_options_row() == PC_ROW_APPLY);
    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_A);
    expect("reset turns both off", pc_options_draft()->a11y_repeat == A11Y_REPEAT_OFF
                                       && pc_options_draft()->a11y_toggle == A11Y_TOGGLE_OFF);
    pc_options_on_esc();

    pc_options_reset();
    s.a11y_repeat = A11Y_REPEAT_B;
    s.a11y_toggle = A11Y_TOGGLE_BOTH;
    pc_options_set_baseline(&s);
    pc_options_set_locks(PC_LOCK_REPEAT | PC_LOCK_TOGGLE);
    pc_options_on_f10();
    while (pc_options_row() != PC_ROW_REPEAT)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_REPEAT, buf, sizeof buf);
    expect("locked repeat says launch script", strstr(buf, "launch script") != NULL);
    pulse(PC_PAD_DPAD_RIGHT);
    expect("locked repeat does not change", pc_options_draft()->a11y_repeat == A11Y_REPEAT_B);
    pulse(PC_PAD_DPAD_DOWN);
    pulse(PC_PAD_DPAD_RIGHT);
    expect("locked toggle does not change", pc_options_draft()->a11y_toggle == A11Y_TOGGLE_BOTH);
    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_A);
    expect("reset keeps locked holds", pc_options_draft()->a11y_repeat == A11Y_REPEAT_B
                                           && pc_options_draft()->a11y_toggle == A11Y_TOGGLE_BOTH);
    pc_options_on_esc();
}

static void test_speed_rows(void)
{
    struct video_settings s = base_settings();
    struct pc_pad_result r;
    char buf[64];
    pc_options_reset();
    pc_options_set_baseline(&s);
    pc_options_set_locks(0);
    pc_options_on_f10();
    expect("speed rows are named",
           strcmp(pc_options_row_title(PC_ROW_FPS60), "60 FPS gameplay") == 0
           && strcmp(pc_options_row_title(PC_ROW_PIPELINE), "Don't wait for renderer") == 0);
    while (pc_options_row() != PC_ROW_FPS60)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_FPS60, buf, sizeof buf);
    expect("60 FPS starts off", strcmp(buf, "Off") == 0 && pc_options_draft()->fps60 == 0);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    pc_options_row_value(PC_ROW_FPS60, buf, sizeof buf);
    expect("right turns 60 FPS on",
           r.action == PC_ACT_PREVIEW && pc_options_draft()->fps60 == 1 && strcmp(buf, "On") == 0);
    pulse(0);
    pulse(PC_PAD_DPAD_DOWN);
    expect("next row is the renderer setting", pc_options_row() == PC_ROW_PIPELINE);
    r = pc_options_on_port(0, PC_PAD_DPAD_LEFT);
    expect("left toggles it too", r.action == PC_ACT_PREVIEW && pc_options_draft()->pipeline == 1);
    pulse(0);
    while (pc_options_row() != PC_ROW_RESET)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_on_port(0, PC_PAD_A);
    expect("reset turns both off", pc_options_draft()->fps60 == 0 && pc_options_draft()->pipeline == 0);
    pc_options_on_esc();

    /* A launch-script value locks the row. */
    pc_options_reset();
    s.fps60 = 1;
    pc_options_set_baseline(&s);
    pc_options_set_locks(PC_LOCK_FPS60);
    pc_options_on_f10();
    while (pc_options_row() != PC_ROW_FPS60)
        pulse(PC_PAD_DPAD_DOWN);
    pc_options_row_value(PC_ROW_FPS60, buf, sizeof buf);
    expect("locked 60 FPS says launch script", strstr(buf, "launch script") != NULL);
    r = pc_options_on_port(0, PC_PAD_DPAD_RIGHT);
    expect("locked 60 FPS does not change", pc_options_draft()->fps60 == 1);
    pc_options_on_esc();
}

int main(void)
{
    test_start_alone();
    test_speed_rows();
    test_vsync_row();
    test_a11y_rows();
    test_window_mode_row_has_exclusive();
    test_chord();
    test_same_poll_chord();
    test_nav_filter_repeat_and_dedup();
    test_apply_reset_revert_and_lock();
    test_save_failure();
    test_back_while_start_held_stays();
    test_second_port_does_not_drop_chord();
    test_layout();
    test_port_zero_drops_stuck_start();
    test_release_ui_drops_stuck_start();
    test_keyboard_only_moves_the_page();
    test_rows_are_real();
    if (g_fail) {
        fprintf(stderr, "%d failed\n", g_fail);
        return 1;
    }
    puts("all passed");
    return 0;
}
