#ifndef MAP_LIST_HOST_H
#define MAP_LIST_HOST_H
/* Fills the developers' MAP VIEWER list (area table [0x2A8E38]) from map_list.txt beside the exe
 * (tools/map_list.py). Called when F8 opens the F026 menu. Returns the number of maps (0 = none). */
int map_list_install(void);
#endif
