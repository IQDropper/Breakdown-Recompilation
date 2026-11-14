/* content_mode.c - "Content" setting (S10.15). See content_mode.h. */
#include "content_mode.h"

#include <string.h>

static volatile int s_mode = CONTENT_CLASSIC;

int content_mode_from_token(const char *s)
{
    return s && (strcmp(s, "new") == 0 || strcmp(s, "New") == 0 || strcmp(s, "1") == 0) ? CONTENT_NEW
                                                                                          : CONTENT_CLASSIC;
}

const char *content_mode_token(int mode)
{
    return mode == CONTENT_NEW ? "new" : "classic";
}

const char *content_mode_label(int mode)
{
    return mode == CONTENT_NEW ? "New" : "Classic";
}

void content_mode_set(int mode)
{
    s_mode = mode == CONTENT_NEW ? CONTENT_NEW : CONTENT_CLASSIC;
}

int content_mode_new(void)
{
    return s_mode == CONTENT_NEW;
}
