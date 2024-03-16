/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"

struct alpaca {
	int alpaca_fd;

	struct termios alpaca_tios;
};

static int alpaca_device_power(struct alpaca *alpaca, int on);
static int alpaca_usb_device_power(struct alpaca *alpaca, int on);

static void *alpaca_open(struct device *dev)
{
	struct alpaca *alpaca;

	dev->has_power_key = true;

	alpaca = calloc(1, sizeof(*alpaca));

	alpaca->alpaca_fd = tty_open(dev->control_dev, &alpaca->alpaca_tios);
	if (alpaca->alpaca_fd < 0)
		err(1, "failed to open %s", dev->control_dev);

	alpaca_device_power(alpaca, 0);

	if (dev->usb_always_on)
		alpaca_usb_device_power(alpaca, 1);
	else
		alpaca_usb_device_power(alpaca, 0);

	usleep(500000);

	return alpaca;
}

static int alpaca_device_power(struct alpaca *alpaca, int on)
{
	char buf[32];
	int n;

	n = sprintf(buf, "devicePower %d\r", !!on);

	return write(alpaca->alpaca_fd, buf, n);
}

static int alpaca_usb_device_power(struct alpaca *alpaca, int on)
{
	char buf[32];
	int n;

	n = sprintf(buf, "usbDevicePower %d\r", !!on);

	return write(alpaca->alpaca_fd, buf, n);
}

static int alpaca_output_bit(struct alpaca *alpaca, int bit, int value)
{
	char buf[32];
	int n;

	n = sprintf(buf, "ttl outputBit %d %d\r", bit, !!value);

	return write(alpaca->alpaca_fd, buf, n);
}

static int alpaca_power_on(struct device *dev)
{
	alpaca_device_power(dev->cdb, 1);

	return 0;
}

static int alpaca_power_off(struct device *dev)
{
	alpaca_device_power(dev->cdb, 0);

	return 0;
}

static int alpaca_power(struct device *dev, bool on)
{
	if (on)
		return alpaca_power_on(dev);
	else
		return alpaca_power_off(dev);
}

static void alpaca_usb(struct device *dev, bool on)
{
	struct alpaca *alpaca = dev->cdb;

	alpaca_usb_device_power(alpaca, on);
}

static void alpaca_key(struct device *dev, int key, bool asserted)
{
	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		alpaca_output_bit(dev->cdb, 2, asserted);
		break;
	case DEVICE_KEY_POWER:
		alpaca_output_bit(dev->cdb, 1, asserted);
		break;
	}
}

const struct control_ops alpaca_ops = {
	.open = alpaca_open,
	.power = alpaca_power,
	.usb = alpaca_usb,
	.key = alpaca_key,
};
