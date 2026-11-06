#include "video_menu_policy.h"

int video_menu_pause_action(unsigned buttons, unsigned prev_buttons, int menu_open)
{
    unsigned rose = buttons & ~prev_buttons;

    if (menu_open)
        return VIDEO_PAUSE_NONE;
    if (rose & VIDEO_PAD_START)
        return VIDEO_PAUSE_OPEN;
    return VIDEO_PAUSE_NONE;
}

unsigned video_menu_filter_buttons(unsigned buttons, int menu_open)
{
    if (!menu_open)
        return buttons;
    return buttons & ~((unsigned)VIDEO_PAD_MENU_NAV);
}
