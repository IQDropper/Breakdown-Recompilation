#ifndef MEM_SEARCH_HOST_H
#define MEM_SEARCH_HOST_H
/* Live memory search over guest RAM, driven by a command file (RECOMP_MEMSEARCH). See mem_search_host.c. */
#include <stdint.h>
void mem_search_start(void);
/* Publish an address under a name; commands can then use $name. Cheap; safe to call every frame. */
void mem_search_name(const char *name, uint32_t va);
#endif
