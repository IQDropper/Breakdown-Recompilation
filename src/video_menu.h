/*
 * video_menu.h - host Video menu. F10 on the D3D11 window.
 *
 * The Xbox Options screen is guest code generated from a dump, and that
 * dump is not in this repo, so the menu is a host window instead of a
 * patched guest screen. docs/video-options.md.
 */
#ifndef VIDEO_MENU_H
#define VIDEO_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* After display_host_init has loaded the config and bound getenv. */
void video_menu_start(void);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_MENU_H */
