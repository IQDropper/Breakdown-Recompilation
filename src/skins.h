/* skins.h - hero outfit choice (S7.2). Pure logic; the hooks live in recomp_manual.c.
 *
 * The game already has outfits: the hero's character entry lists up to 3, each with a tag
 * (0 normal, 4 "T" shirt, 3 researcher disguise). The hero object's +0x601E holds the index of the
 * one shown. RECOMP_HERO_OUTFIT picks one; story outfits (any tag other than 0) always win. */
#ifndef SKINS_H
#define SKINS_H

#define SKINS_GAME (-1)   /* no choice: the game decides */

/* "game"/"" -> SKINS_GAME; "normal"/"0" -> 0; "t"/"4" -> 4; "researcher"/"lab"/"3" -> 3; unknown -> SKINS_GAME */
int skins_parse(const char *s);
/* The tag to show. game_tag: what the game picked. want: the player's choice. loaded: the wanted outfit's
 * models are in memory. Only the normal outfit (tag 0) is replaced, and only by a loaded one. */
int skins_choose(int game_tag, int want, int loaded);
const char *skins_label(int tag);

#endif
