/*
 * Copyright (c) 2016-2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

#include "tty.h"

int tty_open(const char *tty, struct termios *old)
{
	struct termios tios;
	int ret;
	int fd;

	fd = open(tty, O_RDWR | O_NOCTTY | O_EXCL);
	if (fd < 0)
		err(1, "unable to open \"%s\"", tty);

	ret = tcgetattr(fd, old);
	if (ret < 0)
		err(1, "unable to retrieve \"%s\" tios", tty);

	memset(&tios, 0, sizeof(tios));
	tios.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
	tios.c_iflag = IGNPAR;
	tios.c_oflag = 0;

	tcflush(fd, TCIFLUSH);

	ret = tcsetattr(fd, TCSANOW, &tios);
	if (ret < 0)
		err(1, "unable to update \"%s\" tios", tty);

	return fd;
}
