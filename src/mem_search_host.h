#ifndef MEM_SEARCH_HOST_H
#define MEM_SEARCH_HOST_H
/* Live memory search over guest RAM, driven by a command file (RECOMP_MEMSEARCH). See mem_search_host.c. */
#include <stdint.h>
void mem_search_start(void);
/* Publish an address under a name; commands can then use $name. Cheap; safe to call every frame. */
void mem_search_name(const char *name, uint32_t va);
/* "spawn TASK [DIST]" (S10.12): the game thread copies the character created by TASK (a character task made by
 * the level files' CHAR_CREATE handler, 0x413C0) and puts the copy DIST units in front of the hero. Queued here,
 * run by mem_search_take_spawn() from the hero update. */
int mem_search_take_spawn(uint32_t *task, float *dist);
/* "pak load PATH | pak free N | pak list": returns 1 / 2 / 3 (0 = nothing queued). Run from the hero update. */
int mem_search_take_pak(int *slot, char *path, int size);
#endif
