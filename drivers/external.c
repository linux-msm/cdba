/*
 * Copyright (c) 2021-2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
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
