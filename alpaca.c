/*
 * Copyright (c) 2018, Linaro Ltd.
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
#include "alpaca.h"

struct alpaca {
	int alpaca_fd;

	struct termios alpaca_tios;
};

void *alpaca_open(struct device *dev)
{
	struct alpaca *alpaca;

	alpaca = calloc(1, sizeof(*alpaca));

	alpaca->alpaca_fd = tty_open(dev->alpaca_dev, &alpaca->alpaca_tios);
	if (alpaca->alpaca_fd < 0)
		err(1, "failed to open %s", dev->alpaca_dev);

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

int alpaca_power_on(struct device *dev)
{
	alpaca_device_power(dev->cdb, 1);
	alpaca_usb_device_power(dev->cdb, 1);

	/* Volume down */
	alpaca_output_bit(dev->cdb, 2, 1);

	alpaca_output_bit(dev->cdb, 1, 0);
	sleep(1);
	alpaca_output_bit(dev->cdb, 1, 1);
	sleep(1);
	alpaca_output_bit(dev->cdb, 1, 0);

	return 0;
}

int alpaca_power_off(struct device *dev)
{
	alpaca_device_power(dev->cdb, 0);
	alpaca_usb_device_power(dev->cdb, 0);

	return 0;
}

void alpaca_fastboot_key(struct device *dev, bool on)
{
	alpaca_output_bit(dev->cdb, 2, 0);
}
