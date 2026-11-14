/* content_mode.h - "Content" setting (S10.15, Foster 2026-11-13): Classic = the original game, New = our added
 * content (hidden lines in the HD textures, flash scares, new moves). Fixes are not "new content" and stay on in
 * both. Default Classic. RECOMP_CONTENT=classic|new (breakdown_video.cfg or the launch environment). */
#ifndef CONTENT_MODE_H
#define CONTENT_MODE_H
#ifdef __cplusplus
extern "C" {
#endif
#define CONTENT_CLASSIC 0
#define CONTENT_NEW 1
/* "new" -> CONTENT_NEW; anything else (NULL, "classic", unknown) -> CONTENT_CLASSIC. */
int content_mode_from_token(const char *s);
const char *content_mode_token(int mode);   /* "classic" / "new" */
const char *content_mode_label(int mode);   /* "Classic" / "New" */
/* The live choice: set at start from the settings and by Apply on the PC page; new content asks this. */
void content_mode_set(int mode);
int content_mode_new(void);
#ifdef __cplusplus
}
#endif
#endif
