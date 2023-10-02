/*
 * Copyright (c) 2023, Linaro Ltd.
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
#include <unistd.h>

#include "cdba-server.h"
#include "ftdi-gpio.h"

#include <ftdi.h>

enum {
	GPIO_POWER = 0,			// Power input enable
	GPIO_FASTBOOT_KEY,		// Usually volume key
	GPIO_POWER_KEY,			// Key to power the device
	GPIO_USB_DISCONNECT,		// Simulate main USB connection
	GPIO_COUNT
};

enum {
	GPIO_ACTIVE_HIGH = 0,
	GPIO_ACTIVE_LOW,
};

struct ftdi_gpio {
	struct ftdi_context *gpio;
	char *ftdi_device;
	unsigned int ftdi_interface;
	unsigned int gpio_present[GPIO_COUNT];
	unsigned int gpio_offset[GPIO_COUNT];
	unsigned int gpio_polarity[GPIO_COUNT];
	unsigned char gpio_lines;
};

static int ftdi_gpio_device_power(struct ftdi_gpio *ftdi_gpio, bool on);
static void ftdi_gpio_device_usb(struct ftdi_gpio *ftdi_gpio, bool on);

/*
 * fdio_gpio parameter: <libftdi description>;[<interface>[;<gpios>...]]
 * - libftdi description: "s:0xVEND:0xPROD:SERIAL"
 * - interface: A, B, C or D (default A)
 * - gpios: type,id,polarity
 *   - type: POWER, FASTBOOT_KEY, POWER_KEY or USB_DISCONNECT
 *   - id: 0, 1, 2, 3, 4, 5, 6 or 7
 *   - polarity: ACTIVE_HIGH or ACTIVE_LOW
 *
 * Example: s:0xVEND:0xPROD:SERIAL;D;POWER,0,ACTIVE_LOW;FASTBOOT_KEY,1,ACTIVE_HIGH;POWER_KEY,2,ACTIVE_HIGH;USB_DISCONNECT,3,ACTIVE_LOW
 */

static void ftdi_gpio_parse_config(struct ftdi_gpio *ftdi_gpio, char *control_dev)
{
	char *c, *interface;
	size_t device_len;

	// First libftdi description
	c = strchr(control_dev, ';');
	if (!c)
		device_len = strlen(control_dev);
	else
		device_len = c - control_dev;

	ftdi_gpio->ftdi_device = strndup(control_dev, device_len);

	if (!c)
		return;

	// Interface
	interface = c + 1;
	if (*interface != 'A' &&
	    *interface != 'B' &&
	    *interface != 'C' &&
	    *interface != 'D') {
		errx(1, "Invalid interface '%c'", *interface);
	}
	ftdi_gpio->ftdi_interface = *interface - 'A';

	c = strchr(interface, ';');

	// GPIOs
	while(c) {
		char *name, *off, *pol;
		unsigned gpio_type;
		unsigned gpio_offset;
		unsigned gpio_polarity;

		name = c + 1;
		off = strchr(name, ',');
		if (!off)
			errx(1, "GPIOs config invalid");
		off += 1;
		pol = strchr(off, ',');
		if (!pol)
			errx(1, "GPIOs config invalid");
		pol += 1;

		c = strchr(pol, ';');

		if (strncmp("POWER", name, off - name - 1) == 0)
			gpio_type = GPIO_POWER;
		else if (strncmp("FASTBOOT_KEY", name, off - name - 1) == 0)
			gpio_type = GPIO_FASTBOOT_KEY;
		else if (strncmp("POWER_KEY", name, off - name - 1) == 0)
			gpio_type = GPIO_POWER_KEY;
		else if (strncmp("USB_DISCONNECT", name, off - name - 1) == 0)
			gpio_type = GPIO_USB_DISCONNECT;
		else
			errx(1, "GPIOs type invalid: '%s'", name);

		gpio_offset = strtoul(off, NULL, 0);
		if (gpio_offset > 7)
			errx(1, "GPIOs offset invalid: '%u'", gpio_offset);

		if (strncmp("ACTIVE_HIGH", pol, c - pol - 1) == 0)
			gpio_polarity = GPIO_ACTIVE_HIGH;
		else if (strncmp("ACTIVE_LOW", pol, c - pol - 1) == 0)
			gpio_polarity = GPIO_ACTIVE_LOW;
		else
			errx(1, "GPIOs polarity invalid: '%s'", pol);

		ftdi_gpio->gpio_present[gpio_type] = 1;
		ftdi_gpio->gpio_offset[gpio_type] = gpio_offset;
		ftdi_gpio->gpio_polarity[gpio_type] = gpio_polarity;
	}
}

void *ftdi_gpio_open(struct device *dev)
{
	struct ftdi_gpio *ftdi_gpio;
	int ret;

	ftdi_gpio = calloc(1, sizeof(*ftdi_gpio));

	ftdi_gpio_parse_config(ftdi_gpio, dev->control_dev);

	if ((ftdi_gpio->gpio = ftdi_new()) == 0)
		errx(1, "failed to allocate ftdi gpio struct");

	ftdi_set_interface(ftdi_gpio->gpio, INTERFACE_A + ftdi_gpio->ftdi_interface);

	ret = ftdi_usb_open_string(ftdi_gpio->gpio, ftdi_gpio->ftdi_device);
	if (ret < 0)
		errx(1, "failed to open ftdi gpio device '%s' (%d)", ftdi_gpio->ftdi_device, ret);

	ftdi_set_bitmode(ftdi_gpio->gpio, 0xFF, BITMODE_BITBANG);

	if (ftdi_gpio->gpio_present[GPIO_POWER_KEY])
		dev->has_power_key = true;

	ftdi_gpio_device_power(ftdi_gpio, 0);

	if (dev->usb_always_on)
		ftdi_gpio_device_usb(ftdi_gpio, 1);
	else
		ftdi_gpio_device_usb(ftdi_gpio, 0);

	usleep(500000);

	return ftdi_gpio;
}

static int ftdi_gpio_toggle_io(struct ftdi_gpio *ftdi_gpio, unsigned int gpio, bool on)
{
	unsigned int bit;

	if (!ftdi_gpio->gpio_present[gpio])
		return -EINVAL;

	bit = ftdi_gpio->gpio_offset[gpio];

	if (ftdi_gpio->gpio_polarity[gpio])
		on = !on;

	if (on)
		ftdi_gpio->gpio_lines |= (1 << bit);
	else
		ftdi_gpio->gpio_lines &= ~(1 << bit);

	return ftdi_write_data(ftdi_gpio->gpio, &ftdi_gpio->gpio_lines, 1);
}

static int ftdi_gpio_device_power(struct ftdi_gpio *ftdi_gpio, bool on)
{
	return ftdi_gpio_toggle_io(ftdi_gpio, GPIO_POWER, on);
}

static void ftdi_gpio_device_usb(struct ftdi_gpio *ftdi_gpio, bool on)
{
	ftdi_gpio_toggle_io(ftdi_gpio, GPIO_USB_DISCONNECT, on);
}

int ftdi_gpio_power(struct device *dev, bool on)
{
	struct ftdi_gpio *ftdi_gpio = dev->cdb;

	return ftdi_gpio_device_power(ftdi_gpio, on);
}

void ftdi_gpio_usb(struct device *dev, bool on)
{
	struct ftdi_gpio *ftdi_gpio = dev->cdb;

	ftdi_gpio_device_usb(ftdi_gpio, on);
}

void ftdi_gpio_key(struct device *dev, int key, bool asserted)
{
	struct ftdi_gpio *ftdi_gpio = dev->cdb;

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		ftdi_gpio_toggle_io(ftdi_gpio, GPIO_FASTBOOT_KEY, asserted);
		break;
	case DEVICE_KEY_POWER:
		ftdi_gpio_toggle_io(ftdi_gpio, GPIO_POWER_KEY, asserted);
		break;
	}
}

const struct control_ops ftdi_gpio_ops = {
	.open = ftdi_gpio_open,
	.power = ftdi_gpio_power,
	.usb = ftdi_gpio_usb,
	.key = ftdi_gpio_key,
};
