/*
 * Copyright (c) 2021-2023, Linaro Ltd.
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
#include <sys/wait.h>
#include <unistd.h>

#include <errno.h>
#include <stdlib.h>

#include "cdba-server.h"
#include "device.h"

struct external {
	const char *path;
	const char *board;
};

static int external_helper(struct external *ext, const char *command, bool on)
{
	pid_t pid, pid_ret;
	int status;

	pid =  fork();
	switch (pid) {
	case 0:
		/* Do not clobber stdout with program messages or cdba will become confused */
		dup2(2, 1);
		return execlp(ext->path, ext->path, ext->board, command, on ? "on": "off", NULL);
	case -1:
		return -1;
	default:
		break;
	}

	pid_ret = waitpid(pid, &status, 0);
	if (pid_ret < 0)
		return pid_ret;

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		errno = -EINTR;
	else
		errno = -EIO;

	return -1;
}

static void *external_open(struct device *dev)
{
	struct external *ext;

	dev->has_power_key = true;

	ext = calloc(1, sizeof(*ext));

	ext->path = dev->control_dev;
	ext->board = dev->board;

	return ext;
}

static int external_power(struct device *dev, bool on)
{
	struct external *ext = dev->cdb;

	return external_helper(ext, "power", on);
}

static void external_usb(struct device *dev, bool on)
{
	struct external *ext = dev->cdb;

	external_helper(ext, "usb", on);
}

static void external_key(struct device *dev, int key, bool asserted)
{
	struct external *ext = dev->cdb;

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		external_helper(ext, "key-fastboot", asserted);
		break;
	case DEVICE_KEY_POWER:
		external_helper(ext, "key-power", asserted);
		break;
	}
}

const struct control_ops external_ops = {
	.open = external_open,
	.power = external_power,
	.usb = external_usb,
	.key = external_key,
};
