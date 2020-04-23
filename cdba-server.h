#ifndef __BAD_H__
#define __BAD_H__

#include <stdbool.h>

#include "cdba.h"

void watch_add_readfd(int fd, int (*cb)(int, void*), void *data);
int watch_add_quit(int (*cb)(int, void*), void *data);
int watch_add_timer(void (*cb)(void*), void *data, unsigned interval, bool repeat);
void watch_quit(void);
int watch_run(void);

#endif
