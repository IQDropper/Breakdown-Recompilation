/*
 * video_menu.h - starts the in-frame PC options page.
 *
 * The Xbox Options screen is guest code. This entry opens a host page
 * inside the 4:3 picture instead. docs/video-options.md.
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
