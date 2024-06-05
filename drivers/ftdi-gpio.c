/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#define _GNU_SOURCE
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
#include <yaml.h>

#include "cdba-server.h"
#include "device.h"
#include "device_parser.h"

#define TOKEN_LENGTH	16384
#define FTDI_INTERFACE_COUNT	4

#include <ftdi.h>

enum {
	GPIO_POWER = 0,			// Power input enable
	GPIO_FASTBOOT_KEY,		// Usually volume key
	GPIO_POWER_KEY,			// Key to power the device
	GPIO_USB_DISCONNECT,		// Simulate main USB connection
	GPIO_OUTPUT_ENABLE,		// Enable FTDI signals to flow to the board
	GPIO_COUNT
};

struct ftdi_gpio_options {
	struct {
		char *description;
		char *vendor;
		char *product;
		char *serial;
		unsigned int index;
		char *devicenode;
	} ftdi;
	struct {
		bool present;
		unsigned int interface;
		unsigned int offset;
		bool active_low;
	} gpios[GPIO_COUNT];
};

struct ftdi_gpio {
	struct ftdi_gpio_options *options;
	struct ftdi_context *interface[FTDI_INTERFACE_COUNT];
	unsigned char gpio_lines[FTDI_INTERFACE_COUNT];
};

static int ftdi_gpio_device_power(struct ftdi_gpio *ftdi_gpio, bool on);
static void ftdi_gpio_device_usb(struct ftdi_gpio *ftdi_gpio, bool on);
static int ftdi_gpio_toggle_io(struct ftdi_gpio *ftdi_gpio, unsigned int gpio, bool on);

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

static void ftdi_gpio_parse_config(struct ftdi_gpio_options *options, char *value)
{
	unsigned ftdi_interface = 0;
	char *c, *interface;
	size_t device_len;

	// First libftdi description
	c = strchr(value, ';');
	if (!c)
		device_len = strlen(value);
	else
		device_len = c - value;

	options->ftdi.description = strndup(value, device_len);

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
	ftdi_interface = *interface - 'A';

	c = strchr(interface, ';');

	// GPIOs
	while(c) {
		char *name, *off, *pol;
		unsigned gpio_type;
		unsigned gpio_offset;
		bool active_low;

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
		else if (strncmp("OUTPUT_ENABLE", name, off - name - 1) == 0)
			gpio_type = GPIO_OUTPUT_ENABLE;
		else
			errx(1, "GPIOs type invalid: '%s'", name);

		gpio_offset = strtoul(off, NULL, 0);
		if (gpio_offset > 7)
			errx(1, "GPIOs offset invalid: '%u'", gpio_offset);

		if (strncmp("ACTIVE_HIGH", pol, c - pol - 1) == 0)
			active_low = false;
		else if (strncmp("ACTIVE_LOW", pol, c - pol - 1) == 0)
			active_low = true;
		else
			errx(1, "GPIOs polarity invalid: '%s'", pol);

		options->gpios[gpio_type].present = true;
		options->gpios[gpio_type].interface = ftdi_interface;
		options->gpios[gpio_type].offset = gpio_offset;
		options->gpios[gpio_type].active_low = active_low;
	}
}

void *ftdi_gpio_parse_options(struct device_parser *dp)
{
	struct ftdi_gpio_options *options;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	options = calloc(1, sizeof(*options));

	/* Still accept legacy string */
	if (device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH)) {
		warnx("Please switch to yaml config for ftdi_gpio configuration");
		ftdi_gpio_parse_config(options, value);
		return options;
	}

	device_parser_accept(dp, YAML_MAPPING_START_EVENT, NULL, 0);

	while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		unsigned int gpio_id;

		if (!strcmp(key, "power")) {
			gpio_id = GPIO_POWER;
		} else if (!strcmp(key, "fastboot_key")) {
			gpio_id = GPIO_FASTBOOT_KEY;
		} else if (!strcmp(key, "power_key")) {
			gpio_id = GPIO_POWER_KEY;
		} else if (!strcmp(key, "usb_disconnect")) {
			gpio_id = GPIO_USB_DISCONNECT;
		} else if (!strcmp(key, "output_enable")) {
			gpio_id = GPIO_OUTPUT_ENABLE;
		} else {
			if (!device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH))
				errx(1, "%s: expected value for \"%s\"", __func__, key);

			if (!strcmp(key, "vendor")) {
				options->ftdi.vendor = strdup(value);
			} else if (!strcmp(key, "product")) {
				options->ftdi.product = strdup(value);
			} else if (!strcmp(key, "index")) {
				options->ftdi.index = strtoul(value, NULL, 0);
			} else if (!strcmp(key, "serial")) {
				options->ftdi.serial = strdup(value);
			} else if (!strcmp(key, "devicenode")) {
				options->ftdi.devicenode = strdup(value);
			} else
				errx(1, "%s: unknown type \"%s\"", __func__, key);

			continue;
		}

		device_parser_expect(dp, YAML_MAPPING_START_EVENT, NULL, 0);

		while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
			device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH);

			if (!strcmp(key, "line")) {
				options->gpios[gpio_id].offset = strtoul(value, NULL, 0);
			} else if (!strcmp(key, "interface")) {
				if (*value != 'A' &&
				    *value != 'B' &&
				    *value != 'C' &&
				    *value != 'D') {
					errx(1, "Invalid interface '%c' for gpio %u",
					     *value, gpio_id);
				}
				options->gpios[gpio_id].interface = *value - 'A';
			} else if (!strcmp(key, "active_low")) {
				if (!strcmp(value, "true"))
					options->gpios[gpio_id].active_low = true;
			} else {
				errx(1, "%s: unknown option \"%s\"", __func__, key);
				exit(1);
			}
		}

		device_parser_expect(dp, YAML_MAPPING_END_EVENT, NULL, 0);

		options->gpios[gpio_id].present = true;
	}

	device_parser_expect(dp, YAML_MAPPING_END_EVENT, NULL, 0);

	return options;
}

static void *ftdi_gpio_open(struct device *dev)
{
	struct ftdi_gpio *ftdi_gpio;
	int i, ret;

	ftdi_gpio = calloc(1, sizeof(*ftdi_gpio));

	ftdi_gpio->options = dev->control_options;

	/* Setup libftdi string */
	if (!ftdi_gpio->options->ftdi.description) {
		if (ftdi_gpio->options->ftdi.devicenode)
			asprintf(&ftdi_gpio->options->ftdi.description,
				 "d:%s", ftdi_gpio->options->ftdi.devicenode);
		else if (ftdi_gpio->options->ftdi.vendor &&
			 ftdi_gpio->options->ftdi.product &&
			 ftdi_gpio->options->ftdi.serial)
			asprintf(&ftdi_gpio->options->ftdi.description,
				 "s:%s:%s:%s",
				 ftdi_gpio->options->ftdi.vendor,
				 ftdi_gpio->options->ftdi.product,
				 ftdi_gpio->options->ftdi.serial);
		else if (ftdi_gpio->options->ftdi.vendor &&
			 ftdi_gpio->options->ftdi.product &&
			 ftdi_gpio->options->ftdi.index > 0)
			asprintf(&ftdi_gpio->options->ftdi.description,
				 "i:%s:%s:%u",
				 ftdi_gpio->options->ftdi.vendor,
				 ftdi_gpio->options->ftdi.product,
				 ftdi_gpio->options->ftdi.index);
		else if (ftdi_gpio->options->ftdi.vendor &&
			 ftdi_gpio->options->ftdi.product)
			asprintf(&ftdi_gpio->options->ftdi.description,
				 "i:%s:%s",
				 ftdi_gpio->options->ftdi.vendor,
				 ftdi_gpio->options->ftdi.product);
		else
			errx(1, "Incomplete FTDI description properties");
	}

	for (i = 0; i < GPIO_COUNT; ++i) {
		unsigned int ftdi_interface;

		if (!ftdi_gpio->options->gpios[i].present)
			continue;

		ftdi_interface = ftdi_gpio->options->gpios[i].interface;

		/* Skip if interface already opened */
		if (ftdi_gpio->interface[ftdi_interface])
			continue;

		if ((ftdi_gpio->interface[ftdi_interface] = ftdi_new()) == 0)
			errx(1, "failed to allocate ftdi gpio struct");

		ftdi_set_interface(ftdi_gpio->interface[ftdi_interface],
				   INTERFACE_A + ftdi_interface);

		ret = ftdi_usb_open_string(ftdi_gpio->interface[ftdi_interface],
					   ftdi_gpio->options->ftdi.description);
		if (ret < 0)
			errx(1, "failed to open ftdi gpio device '%s' (%d)",
			     ftdi_gpio->options->ftdi.description, ret);

		ftdi_set_bitmode(ftdi_gpio->interface[ftdi_interface],
				 0xFF, BITMODE_BITBANG);
	}

	if (ftdi_gpio->options->gpios[GPIO_POWER_KEY].present)
		dev->has_power_key = true;

	if (ftdi_gpio->options->gpios[GPIO_OUTPUT_ENABLE].present)
		ftdi_gpio_toggle_io(ftdi_gpio, GPIO_OUTPUT_ENABLE, 1);

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
	unsigned int ftdi_interface;
	unsigned int bit;

	if (!ftdi_gpio->options->gpios[gpio].present)
		return -EINVAL;

	ftdi_interface = ftdi_gpio->options->gpios[gpio].interface;

	bit = ftdi_gpio->options->gpios[gpio].offset;

	if (ftdi_gpio->options->gpios[gpio].active_low)
		on = !on;

	if (on)
		ftdi_gpio->gpio_lines[ftdi_interface] |= (1 << bit);
	else
		ftdi_gpio->gpio_lines[ftdi_interface] &= ~(1 << bit);

	return ftdi_write_data(ftdi_gpio->interface[ftdi_interface],
			       &ftdi_gpio->gpio_lines[ftdi_interface], 1);
}

static int ftdi_gpio_device_power(struct ftdi_gpio *ftdi_gpio, bool on)
{
	return ftdi_gpio_toggle_io(ftdi_gpio, GPIO_POWER, on);
}

static void ftdi_gpio_device_usb(struct ftdi_gpio *ftdi_gpio, bool on)
{
	ftdi_gpio_toggle_io(ftdi_gpio, GPIO_USB_DISCONNECT, on);
}

static int ftdi_gpio_power(struct device *dev, bool on)
{
	struct ftdi_gpio *ftdi_gpio = dev->cdb;

	return ftdi_gpio_device_power(ftdi_gpio, on);
}

static void ftdi_gpio_usb(struct device *dev, bool on)
{
	struct ftdi_gpio *ftdi_gpio = dev->cdb;

	ftdi_gpio_device_usb(ftdi_gpio, on);
}

static void ftdi_gpio_key(struct device *dev, int key, bool asserted)
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
	.parse_options = ftdi_gpio_parse_options,
	.open = ftdi_gpio_open,
	.power = ftdi_gpio_power,
	.usb = ftdi_gpio_usb,
	.key = ftdi_gpio_key,
};
