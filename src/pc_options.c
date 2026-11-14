#include "pc_options.h"

#include <stdio.h>
#include <string.h>

static struct video_settings s_base;
static struct video_settings s_draft;
static struct video_settings s_committed;
static int s_have_base;
static unsigned s_locks;
static int s_open;
static int s_row;
static unsigned s_port[4];
static unsigned s_ui;
static unsigned s_prev;
static int s_vhold;
static int s_vdir;
static int s_hhold;
static int s_hdir;
static int s_save_pending;
static char s_status[160];

static unsigned merged(void)
{
    unsigned m = s_ui;
    int i;
    for (i = 0; i < 4; i++)
        m |= s_port[i];
    return m;
}

static int lock_for_row(int row)
{
    static const unsigned bits[PC_ROW_LAST_SETTING + 1] = {
        PC_LOCK_SCALE, PC_LOCK_WINDOW, PC_LOCK_ASPECT, PC_LOCK_WIDE, PC_LOCK_FILTER, PC_LOCK_MODE,
        PC_LOCK_FPS60, PC_LOCK_PIPE, PC_LOCK_VSYNC, PC_LOCK_REPEAT, PC_LOCK_TOGGLE, PC_LOCK_OUTFIT,
        PC_LOCK_CONTENT, PC_LOCK_TEXT, PC_LOCK_MOUSE, PC_LOCK_INVERT, PC_LOCK_DEADZONE, PC_LOCK_KEYS
    };
    if (row < 0 || row > PC_ROW_LAST_SETTING)
        return 0;
    return (s_locks & bits[row]) != 0;
}

static void set_status(const char *text)
{
    snprintf(s_status, sizeof s_status, "%s", text ? text : "");
}

static void open_page(struct pc_pad_result *r)
{
    if (!s_have_base)
        video_settings_stock(&s_base);
    s_draft = s_base;
    s_committed = s_base;
    video_settings_clamp(&s_draft);
    video_settings_clamp(&s_committed);
    s_open = 1;
    s_row = 0;
    s_vhold = s_hhold = 0;
    s_vdir = s_hdir = 0;
    s_save_pending = 0;
    set_status("PC options. Not the Xbox Options screen. Nothing is saved yet.");
    r->action = PC_ACT_OPENED;
}

static void revert_close(struct pc_pad_result *r)
{
    s_draft = s_committed;
    s_open = 0;
    s_save_pending = 0;
    s_vhold = s_hhold = 0;
    s_vdir = s_hdir = 0;
    set_status("Reverted. The file is unchanged.");
    r->action = PC_ACT_REVERT;
}

static void move_row(int dir)
{
    s_row += dir;
    if (s_row < 0)
        s_row = PC_ROW_COUNT - 1;
    if (s_row >= PC_ROW_COUNT)
        s_row = 0;
}

static int nudge(int dir, struct pc_pad_result *r)
{
    int video_row;
    if (s_row > PC_ROW_LAST_SETTING || !dir)
        return 0;
    if (lock_for_row(s_row)) {
        set_status("Launch script owns this row. It overrides the file.");
        return 0;
    }
    video_row = s_row;
    if (!video_settings_nudge(&s_draft, video_row, dir))
        return 0;
    set_status("Preview only. Apply writes the file. Revert puts the picture back.");
    if (r->action == PC_ACT_NONE)
        r->action = PC_ACT_PREVIEW;
    return 1;
}

static void activate(struct pc_pad_result *r)
{
    struct video_settings stock;

    if (s_row == PC_ROW_APPLY) {
        s_save_pending = 1;
        set_status("Apply writes the file.");
        r->action = PC_ACT_SAVE;
        return;
    }
    if (s_row == PC_ROW_REVERT) {
        revert_close(r);
        return;
    }
    if (s_row == PC_ROW_RECOMMENDED) {
        struct video_settings rec = s_draft;
        video_settings_recommended(&rec);
        if (!lock_for_row(PC_ROW_SCALE))
            s_draft.render_scale = rec.render_scale;
        if (!lock_for_row(PC_ROW_ASPECT))
            s_draft.aspect = rec.aspect;
        if (!lock_for_row(PC_ROW_WIDE))
            s_draft.horplus = rec.horplus;
        if (!lock_for_row(PC_ROW_FILTER))
            s_draft.filter = rec.filter;
        if (!lock_for_row(PC_ROW_FPS60))
            s_draft.fps60 = rec.fps60;
        if (!lock_for_row(PC_ROW_PIPELINE))
            s_draft.pipeline = rec.pipeline;
        if (!lock_for_row(PC_ROW_VSYNC))
            s_draft.vsync = rec.vsync;
        video_settings_clamp(&s_draft);
        set_status("Recommended preview. Apply writes it. Wide view and Don't wait apply on the next start.");
        r->action = PC_ACT_PREVIEW;
        return;
    }
    if (s_row != PC_ROW_RESET)
        return;
    video_settings_stock(&stock);
    if (!lock_for_row(PC_ROW_SCALE))
        s_draft.render_scale = stock.render_scale;
    if (!lock_for_row(PC_ROW_WINDOW))
        s_draft.window_scale = stock.window_scale;
    if (!lock_for_row(PC_ROW_ASPECT))
        s_draft.aspect = stock.aspect;
    if (!lock_for_row(PC_ROW_WIDE))
        s_draft.horplus = stock.horplus;
    if (!lock_for_row(PC_ROW_FILTER))
        s_draft.filter = stock.filter;
    if (!lock_for_row(PC_ROW_MODE))
        s_draft.borderless = stock.borderless;
    if (!lock_for_row(PC_ROW_FPS60))
        s_draft.fps60 = stock.fps60;
    if (!lock_for_row(PC_ROW_PIPELINE))
        s_draft.pipeline = stock.pipeline;
    if (!lock_for_row(PC_ROW_VSYNC))
        s_draft.vsync = stock.vsync;
    if (!lock_for_row(PC_ROW_REPEAT))
        s_draft.a11y_repeat = stock.a11y_repeat;
    if (!lock_for_row(PC_ROW_TOGGLE))
        s_draft.a11y_toggle = stock.a11y_toggle;
    if (!lock_for_row(PC_ROW_OUTFIT))
        s_draft.outfit = stock.outfit;
    if (!lock_for_row(PC_ROW_CONTENT))
        s_draft.content = stock.content;
    if (!lock_for_row(PC_ROW_TEXT))
        s_draft.text_size = stock.text_size;
    if (!lock_for_row(PC_ROW_MOUSE))
        s_draft.mouse_speed = stock.mouse_speed;
    if (!lock_for_row(PC_ROW_INVERT))
        s_draft.mouse_invert = stock.mouse_invert;
    if (!lock_for_row(PC_ROW_DEADZONE))
        s_draft.deadzone = stock.deadzone;
    if (!lock_for_row(PC_ROW_KEYS))
        memcpy(s_draft.keymap, stock.keymap, sizeof s_draft.keymap);
    video_settings_clamp(&s_draft);
    set_status("Stock preview. Apply writes it. Revert restores the last applied settings.");
    r->action = PC_ACT_PREVIEW;
}

static void repeat_axis(int *hold, int dir, int vertical, struct pc_pad_result *r)
{
    if (!dir) {
        *hold = 0;
        if (vertical)
            s_vdir = 0;
        else
            s_hdir = 0;
        return;
    }
    if ((vertical ? s_vdir : s_hdir) != dir) {
        *hold = 0;
        return;
    }
    (*hold)++;
    if (*hold >= 8 && ((*hold - 8) % 4) == 0) {
        if (vertical)
            move_row(dir);
        else
            nudge(dir, r);
    }
}

static struct pc_pad_result evaluate(unsigned port_buttons, int have_port, int advance)
{
    struct pc_pad_result r;
    unsigned now, prev, rose;
    int up_edge, down_edge, left_edge, right_edge;
    int vdir, hdir;
    int eat;

    memset(&r, 0, sizeof r);
    r.guest_buttons = have_port ? port_buttons : 0;
    now = merged();
    prev = s_prev;
    rose = now & ~prev;

    if (!s_open && ((rose & PC_PAD_START) && (now & PC_PAD_BACK)))
        open_page(&r);
    else if (!s_open && ((rose & PC_PAD_BACK) && (now & PC_PAD_START)))
        open_page(&r);
    else if (s_open) {
        up_edge = (rose & PC_PAD_DPAD_UP) && !(rose & PC_PAD_DPAD_DOWN);
        down_edge = (rose & PC_PAD_DPAD_DOWN) && !(rose & PC_PAD_DPAD_UP);
        left_edge = (rose & PC_PAD_DPAD_LEFT) && !(rose & PC_PAD_DPAD_RIGHT);
        right_edge = (rose & PC_PAD_DPAD_RIGHT) && !(rose & PC_PAD_DPAD_LEFT);
        if (up_edge || down_edge) {
            vdir = down_edge ? 1 : -1;
            move_row(vdir);
            s_vdir = vdir;
            s_vhold = 0;
        } else if (advance) {
            vdir = 0;
            if ((now & PC_PAD_DPAD_DOWN) && !(now & PC_PAD_DPAD_UP))
                vdir = 1;
            else if ((now & PC_PAD_DPAD_UP) && !(now & PC_PAD_DPAD_DOWN))
                vdir = -1;
            repeat_axis(&s_vhold, vdir, 1, &r);
        }
        if (r.action != PC_ACT_REVERT) {
            if (left_edge || right_edge) {
                hdir = right_edge ? 1 : -1;
                nudge(hdir, &r);
                s_hdir = hdir;
                s_hhold = 0;
            } else if (advance) {
                hdir = 0;
                if ((now & PC_PAD_DPAD_RIGHT) && !(now & PC_PAD_DPAD_LEFT))
                    hdir = 1;
                else if ((now & PC_PAD_DPAD_LEFT) && !(now & PC_PAD_DPAD_RIGHT))
                    hdir = -1;
                repeat_axis(&s_hhold, hdir, 0, &r);
            }
        }
        if (r.action != PC_ACT_REVERT && (rose & PC_PAD_A))
            activate(&r);
        if (r.action != PC_ACT_REVERT && (rose & PC_PAD_B))
            revert_close(&r);
        if (r.action != PC_ACT_REVERT && (rose & PC_PAD_BACK) && !(now & PC_PAD_START))
            revert_close(&r);
    }

    s_prev = now;
    eat = s_open || r.action == PC_ACT_OPENED || r.action == PC_ACT_REVERT;
    if (have_port)
        r.guest_buttons = eat ? (port_buttons & ~PC_PAD_NAV) : port_buttons;
    r.suppress_motion = eat ? 1 : 0;
    return r;
}

void pc_options_reset(void)
{
    memset(s_port, 0, sizeof s_port);
    s_ui = 0;
    s_prev = 0;
    s_open = 0;
    s_row = 0;
    s_locks = 0;
    s_have_base = 0;
    s_vhold = s_hhold = 0;
    s_vdir = s_hdir = 0;
    s_save_pending = 0;
    video_settings_stock(&s_base);
    s_draft = s_base;
    s_committed = s_base;
    s_status[0] = 0;
}

void pc_options_set_baseline(const struct video_settings *s)
{
    if (!s || s_open)
        return;
    s_base = *s;
    video_settings_clamp(&s_base);
    s_have_base = 1;
    s_draft = s_base;
    s_committed = s_base;
}

void pc_options_set_locks(unsigned mask)
{
    if (s_open)
        return;
    s_locks = mask;
}

struct pc_pad_result pc_options_on_port(int port, unsigned buttons)
{
    struct pc_pad_result skip;
    if (port < 0 || port > 3) {
        memset(&skip, 0, sizeof skip);
        skip.guest_buttons = buttons;
        return skip;
    }
    s_port[port] = buttons;
    return evaluate(buttons, 1, 1);
}

struct pc_pad_result pc_options_set_ui_bit(unsigned bit, int down)
{
    int repeat = down && (s_ui & bit);
    if (down)
        s_ui |= bit;
    else
        s_ui &= ~bit;
    return evaluate(0, 0, repeat ? 1 : 0);
}

void pc_options_release_ui(void)
{
    s_ui = 0;
    evaluate(0, 0, 0);
}

struct pc_pad_result pc_options_on_f10(void)
{
    struct pc_pad_result r;
    memset(&r, 0, sizeof r);
    if (s_open)
        revert_close(&r);
    else
        open_page(&r);
    r.suppress_motion = 1;
    return r;
}

struct pc_pad_result pc_options_on_esc(void)
{
    struct pc_pad_result r;
    memset(&r, 0, sizeof r);
    if (s_open)
        revert_close(&r);
    r.suppress_motion = s_open || r.action == PC_ACT_REVERT;
    return r;
}

void pc_options_set_note(const char *text)
{
    if (s_open && text)
        set_status(text);
}

int pc_options_set_keymap(const char *text)
{
    if (!s_open || lock_for_row(PC_ROW_KEYS))
        return 0;
    snprintf(s_draft.keymap, sizeof s_draft.keymap, "%s", text ? text : "");
    set_status("Keys changed. Apply saves them. Revert restores the applied keys.");
    return 1;
}

void pc_options_save_result(int ok)
{
    if (!s_save_pending)
        return;
    if (!ok) {
        set_status("Could not save. The file is unchanged.");
        return;
    }
    s_committed = s_draft;
    s_save_pending = 0;
    set_status("Saved. A launch script still overrides a locked row on the next launch.");
}

int pc_options_is_open(void) { return s_open; }
int pc_options_row(void) { return s_row; }
const struct video_settings *pc_options_draft(void) { return &s_draft; }
const struct video_settings *pc_options_committed(void) { return &s_committed; }
const char *pc_options_status(void) { return s_status; }
int pc_options_row_locked(int row) { return lock_for_row(row); }

const char *pc_options_row_title(int row)
{
    static const char *const titles[PC_ROW_COUNT] = {
        "Internal resolution",
        "Window size",
        "Aspect",
        "Wide view",
        "Present filter",
        "Window mode",
        "60 FPS gameplay",
        "Don't wait for renderer",
        "VSync",
        "Hold to repeat",
        "Hold toggle",
        "Outfit",
        "Content",
        "Text size",
        "Mouse speed",
        "Invert mouse",
        "Stick dead zone",
        "Keys",
        "Apply",
        "Recommended",
        "Reset",
        "Revert",
    };
    if (row < 0 || row >= PC_ROW_COUNT)
        return "";
    return titles[row];
}

void pc_options_row_value(int row, char *buf, size_t n)
{
    char raw[32];
    if (!buf || n == 0)
        return;
    buf[0] = 0;
    if (row < 0 || row > PC_ROW_LAST_SETTING)
        return;
    video_settings_value(&s_draft, row, raw, sizeof raw);
    if (lock_for_row(row))
        snprintf(buf, n, "%s  launch script", raw);
    else
        snprintf(buf, n, "%s", raw);
}

void pc_options_frame_rect(unsigned win_w, unsigned win_h, struct pc_rect *out)
{
    unsigned fw, fh;
    if (!out)
        return;
    out->x = out->y = out->w = out->h = 0;
    if (!win_w || !win_h)
        return;
    if (win_w * 3u >= win_h * 4u) {
        fh = win_h;
        fw = (win_h * 4u) / 3u;
        if (fw > win_w)
            fw = win_w;
        out->x = (int)((win_w - fw) / 2u);
        out->y = 0;
    } else {
        fw = win_w;
        fh = (win_w * 3u) / 4u;
        if (fh > win_h)
            fh = win_h;
        out->x = 0;
        out->y = (int)((win_h - fh) / 2u);
    }
    out->w = (int)fw;
    out->h = (int)fh;
}

void pc_options_panel_rect(unsigned win_w, unsigned win_h, struct pc_rect *out)
{
    struct pc_rect frame;
    int margin;
    if (!out)
        return;
    pc_options_frame_rect(win_w, win_h, &frame);
    margin = frame.w / 16;
    if (margin < 12)
        margin = 12;
    if (margin * 2 >= frame.w)
        margin = frame.w / 10;
    if (margin * 2 >= frame.h)
        margin = frame.h / 10;
    if (margin < 1)
        margin = 1;
    out->x = frame.x + margin;
    out->y = frame.y + margin;
    out->w = frame.w - margin * 2;
    out->h = frame.h - margin * 2;
    if (out->w < 1)
        out->w = 1;
    if (out->h < 1)
        out->h = 1;
}
