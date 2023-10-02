/*
 * Copyright (c) 2020, Linaro Ltd.
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
#include <sys/file.h>
#include <sys/stat.h>

#include <err.h>
#include <stdlib.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"

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

void *console_open(struct device *device)
{
	struct console *console;

	console = calloc(1, sizeof(*console));
	console->console_fd = tty_open(device->console_dev, &console->console_tios);
	if (console->console_fd < 0)
		err(1, "failed to open %s", device->console_dev);

	watch_add_readfd(console->console_fd, console_data, device);

	return console;
}

int console_write(struct device *device, const void *buf, size_t len)
{
	struct console *console = device->console;

	return write(console->console_fd, buf, len);;
}

void console_send_break(struct device *device)
{
	struct console *console = device->console;

	tcsendbreak(console->console_fd, 0);
}

const struct console_ops console_ops = {
	.open = console_open,
	.write = console_write,
	.send_break = console_send_break,
};
