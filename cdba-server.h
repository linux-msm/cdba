#ifndef __BAD_H__
#define __BAD_H__

#include <stdbool.h>
#include <termios.h>

#include "cdba.h"

int tty_open(const char *tty, struct termios *old);

void cdba_send_buf(int type, size_t len, const void *buf);
#define cdba_send(type) cdba_send_buf(type, 0, NULL)

#endif
