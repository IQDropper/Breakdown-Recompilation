/*
 * video_menu.c - starts the PC options host overlay.
 *
 * F10 and Back+Start open an owned borderless window positioned over the
 * 4:3 picture. It is not drawn into the frame and it is not the Xbox
 * Options screen.
 */
#include "video_menu.h"
#include "pc_options_host.h"

void video_menu_start(void)
{
    pc_options_host_start();
}
