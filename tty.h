#ifndef __TTY_H__
#define __TTY_H__

struct termios;
int tty_open(const char *tty, struct termios *old);

#endif
