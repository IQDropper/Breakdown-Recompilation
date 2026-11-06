/*
 * video_menu_policy.h - when Start/pause should open the host Video menu.
 *
 * The Xbox Options screen is guest code. This decision stays on the host:
 * a rising Start (keyboard Enter or the controller Start button) asks the
 * existing F10 window to open. The same press is still delivered to the
 * game, so the pause screen opens underneath. While that window is open,
 * Start and the d-pad are held back so the pause screen does not move.
 */
#ifndef VIDEO_MENU_POLICY_H
#define VIDEO_MENU_POLICY_H

/* Xbox XINPUT_GAMEPAD bits. The input bridge copies this layout. */
#define VIDEO_PAD_DPAD_UP    0x0001u
#define VIDEO_PAD_DPAD_DOWN  0x0002u
#define VIDEO_PAD_DPAD_LEFT  0x0004u
#define VIDEO_PAD_DPAD_RIGHT 0x0008u
#define VIDEO_PAD_START      0x0010u
#define VIDEO_PAD_BACK       0x0020u
#define VIDEO_PAD_A          0x1000u

#define VIDEO_PAD_MENU_NAV (VIDEO_PAD_DPAD_UP | VIDEO_PAD_DPAD_DOWN | \
                            VIDEO_PAD_DPAD_LEFT | VIDEO_PAD_DPAD_RIGHT | \
                            VIDEO_PAD_START)

enum {
    VIDEO_PAUSE_NONE = 0,
    VIDEO_PAUSE_OPEN = 1
};

/* Rising Start while the host menu is closed. A held Start does not repeat.
 * Start while the menu is already open (F10 or a previous pause) does not
 * open it again and does not close it: Enter confirms a row instead. */
int video_menu_pause_action(unsigned buttons, unsigned prev_buttons, int menu_open);

/* menu_open: drop Start and the d-pad. Other buttons pass through.
 * menu closed: return buttons unchanged, including the Start that pauses. */
unsigned video_menu_filter_buttons(unsigned buttons, int menu_open);

#endif /* VIDEO_MENU_POLICY_H */
