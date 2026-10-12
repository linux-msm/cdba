/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "device.h"
#include "tty.h"

struct pic32cx {
	int fd;
	struct termios tios;
	bool fastboot_pressed;
};

static void pic32cx_device_write(struct pic32cx *pic32cx, const char *fmt, ...)
{
	char buf[32];
	va_list va;
	int count;
	va_start(va, fmt);
	count = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	write(pic32cx->fd, buf, count);
}

static void pic32cx_device_power(struct pic32cx *pic32cx, int on)
{
	pic32cx_device_write(pic32cx, "PWR_OFF %d\r", !on);
}

static void *pic32cx_open(struct device *dev)
{
	struct pic32cx *pic32cx;

	dev->has_power_key = true;

	pic32cx = calloc(1, sizeof(*pic32cx));

	pic32cx->fd = tty_open(dev->control_dev, &pic32cx->tios);
	if (pic32cx->fd < 0) {
		free(pic32cx);
		return NULL;
	}

	pic32cx_device_power(pic32cx, 1);

	sleep(5);

	return pic32cx;
}

static int pic32cx_power(struct device *dev, bool on)
{
	pic32cx_device_power(dev->cdb, on);

	return 0;
}

static void pic32cx_key(struct device *dev, int key, bool asserted)
{
	struct pic32cx *pic32cx = dev->cdb;

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		if (asserted)
			pic32cx->fastboot_pressed = true;
		if (!asserted && pic32cx->fastboot_pressed) {
			pic32cx_device_write(pic32cx, "MD_FASTBOOT\r");
			pic32cx->fastboot_pressed = false;
		}
		break;
	}
}

const struct control_ops pic32cx_ops = {
	.open = pic32cx_open,
	.power = pic32cx_power,
	.key = pic32cx_key,
};
