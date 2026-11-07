/*
 * video_menu.c - starts the in-frame PC options page.
 *
 * The captioned desktop popup is gone. F10 and Back+Start open the child
 * window owned by pc_options_host.c. That window is host UI inside the
 * game's client area. It is not the Xbox Options screen.
 */
#include "video_menu.h"
#include "pc_options_host.h"

void video_menu_start(void)
{
    pc_options_host_start();
}
