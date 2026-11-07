/*
 * video_menu.h - starts the PC options host overlay.
 *
 * The Xbox Options screen is guest code. This entry opens an owned
 * borderless window over the 4:3 picture. It is not drawn into the frame.
 * docs/video-options.md.
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
