/*
 * pc_options.h - host PC options page (not the Xbox Options screen).
 *
 * The model is single-threaded. The Win32 host runs it on the game
 * window's thread. docs/superpowers/specs/2026-11-06-pc-options-design.md
 */
#ifndef PC_OPTIONS_H
#define PC_OPTIONS_H

#include "video_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_PAD_DPAD_UP    0x0001u
#define PC_PAD_DPAD_DOWN  0x0002u
#define PC_PAD_DPAD_LEFT  0x0004u
#define PC_PAD_DPAD_RIGHT 0x0008u
#define PC_PAD_START      0x0010u
#define PC_PAD_BACK       0x0020u
#define PC_PAD_A          0x1000u
#define PC_PAD_B          0x2000u

#define PC_PAD_NAV (PC_PAD_DPAD_UP | PC_PAD_DPAD_DOWN | PC_PAD_DPAD_LEFT | \
                    PC_PAD_DPAD_RIGHT | PC_PAD_START | PC_PAD_BACK | \
                    PC_PAD_A | PC_PAD_B)

#define PC_LOCK_SCALE  0x01u
#define PC_LOCK_WINDOW 0x02u
#define PC_LOCK_ASPECT 0x04u
#define PC_LOCK_FILTER 0x08u
#define PC_LOCK_MODE   0x10u

enum {
    PC_ROW_SCALE = 0,
    PC_ROW_WINDOW,
    PC_ROW_ASPECT,
    PC_ROW_FILTER,
    PC_ROW_MODE,
    PC_ROW_APPLY,
    PC_ROW_RESET,
    PC_ROW_REVERT,
    PC_ROW_COUNT
};

enum {
    PC_ACT_NONE = 0,
    PC_ACT_OPENED,
    PC_ACT_PREVIEW,
    PC_ACT_SAVE,
    PC_ACT_REVERT
};

struct pc_pad_result {
    unsigned guest_buttons;
    int suppress_motion; /* clear sticks and triggers for this poll */
    int action;
};

struct pc_rect {
    int x, y, w, h;
};

/* Clears the page, the ports, and the UI bits. */
void pc_options_reset(void);

/* Copied in when the page opens. Ignored while the page is open. */
void pc_options_set_baseline(const struct video_settings *s);
void pc_options_set_locks(unsigned mask);

/* One port, 0..3. Merges with the other ports and with UI-held bits. */
struct pc_pad_result pc_options_on_port(int port, unsigned buttons);

/* Keyboard mirror of one XINPUT bit. down=0 releases it. */
struct pc_pad_result pc_options_set_ui_bit(unsigned bit, int down);

/* F10 toggles. Esc reverts when open. */
struct pc_pad_result pc_options_on_f10(void);
struct pc_pad_result pc_options_on_esc(void);

/* After PC_ACT_SAVE. ok=0 leaves the committed settings and the file alone. */
void pc_options_save_result(int ok);

int pc_options_is_open(void);
int pc_options_row(void);
const struct video_settings *pc_options_draft(void);
const struct video_settings *pc_options_committed(void);
const char *pc_options_row_title(int row);
/* Value text. Locked rows end with "launch script". Action rows are empty. */
void pc_options_row_value(int row, char *buf, size_t n);
const char *pc_options_status(void);
int pc_options_row_locked(int row);

/* 4:3 picture inside the client, then the panel inside that picture. */
void pc_options_frame_rect(unsigned win_w, unsigned win_h, struct pc_rect *out);
void pc_options_panel_rect(unsigned win_w, unsigned win_h, struct pc_rect *out);

#ifdef __cplusplus
}
#endif

#endif /* PC_OPTIONS_H */
