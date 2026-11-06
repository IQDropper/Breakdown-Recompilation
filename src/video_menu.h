/*
 * video_menu.h - host Video menu. F10, or Start/pause, on the D3D11 window.
 *
 * The Xbox Options screen is guest code generated from a dump, and that
 * dump is not in this repo, so the menu is a host window instead of a
 * patched guest screen. docs/video-options.md.
 */
#ifndef VIDEO_MENU_H
#define VIDEO_MENU_H

#include "video_menu_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* After display_host_init has loaded the config and bound getenv.
 * `applied_mode` is the fullscreen value handed to nv2a_host_opts
 * (DISPLAY_WINDOWED, DISPLAY_BORDERLESS, or DISPLAY_EXCLUSIVE). */
void video_menu_start(int applied_mode);

/* 1 while the host Video window is shown. Safe to call from the guest
 * input poll; the window thread publishes it. */
int video_menu_is_open(void);

/* VIDEO_PAUSE_OPEN posts a request onto the game window. VIDEO_PAUSE_NONE
 * does nothing. The window thread is the one that creates the menu. */
void video_menu_on_pause_action(int action);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_MENU_H */
