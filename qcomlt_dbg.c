/*
 * Copyright (c) 2021, Linaro Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cdba-server.h"
#include "qcomlt_dbg.h"

struct qcomlt_dbg {
	int fd;
	struct termios orig_tios;
};

void *qcomlt_dbg_open(struct device *dev)
{
	struct qcomlt_dbg *dbg;

	dev->has_power_key = true;

	dbg = calloc(1, sizeof(*dbg));

	dbg->fd = tty_open(dev->control_dev, &dbg->orig_tios);
	if (dbg->fd < 0)
		err(1, "failed to open %s", dev->control_dev);

	// fprintf(stderr, "qcomlt_dbg_open()\n");
	write(dbg->fd, "brpu", 4);

	return dbg;
}

int qcomlt_dbg_power(struct device *dev, bool on)
{
	struct qcomlt_dbg *dbg = dev->cdb;	

	// fprintf(stderr, "qcomlt_dbg_power(%d)\n", on);
	return write(dbg->fd, &("pP"[on]), 1);
}

void qcomlt_dbg_usb(struct device *dev, bool on)
{
	struct qcomlt_dbg *dbg = dev->cdb;	

	// fprintf(stderr, "qcomlt_dbg_usb(%d)\n", on);
	write(dbg->fd, &("uU"[on]), 1);
}

void qcomlt_dbg_key(struct device *dev, int key, bool asserted)
{
	struct qcomlt_dbg *dbg = dev->cdb;	

	// fprintf(stderr, "qcomlt_dbg_key(%d, %d)\n", key, asserted);

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		write(dbg->fd, &("rR"[asserted]), 1);
		break;
	case DEVICE_KEY_POWER:
		write(dbg->fd, &("bB"[asserted]), 1);
		break;
	}
}
