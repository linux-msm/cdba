/*
 * Copyright (c) 2020, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/file.h>
#include <sys/stat.h>

#include <err.h>
#include <stdlib.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"
#include "tty.h"
#include "watch.h"

struct console {
	int console_fd;
	struct termios console_tios;
};

static int console_data(int fd, void *data)
{
	char buf[128];
	ssize_t n;

	n = read(fd, buf, sizeof(buf));
	if (n < 0)
		return n;

	cdba_send_buf(MSG_CONSOLE, n, buf);

	return 0;
}

static void *console_open(struct device *device)
{
	struct console *console;

	console = calloc(1, sizeof(*console));
	console->console_fd = tty_open(device->console_dev, &console->console_tios);
	if (console->console_fd < 0)
		err(1, "failed to open %s", device->console_dev);

	watch_add_readfd(console->console_fd, console_data, device);

	return console;
}

static int console_write(struct device *device, const void *buf, size_t len)
{
	struct console *console = device->console;

	return write(console->console_fd, buf, len);;
}

static void console_send_break(struct device *device)
{
	struct console *console = device->console;

	tcsendbreak(console->console_fd, 0);
}

const struct console_ops console_ops = {
	.open = console_open,
	.write = console_write,
	.send_break = console_send_break,
};
