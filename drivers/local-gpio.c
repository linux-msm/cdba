/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
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
#include "local-gpio.h"

#define TOKEN_LENGTH	16384

static int local_gpio_device_power(struct local_gpio *local_gpio, bool on);
static void local_gpio_device_usb(struct local_gpio *local_gpio, bool on);

void *local_gpio_parse_options(struct device_parser *dp)
{
	struct local_gpio_options *options;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	device_parser_expect(dp, YAML_MAPPING_START_EVENT, NULL, 0);

	options = calloc(1, sizeof(*options));

	/* Loop over sub-properties */
	while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		int gpio_id;

		if (!strcmp(key, "power")) {
			gpio_id = GPIO_POWER;
		} else if (!strcmp(key, "fastboot_key")) {
			gpio_id = GPIO_FASTBOOT_KEY;
		} else if (!strcmp(key, "power_key")) {
			gpio_id = GPIO_POWER_KEY;
		} else if (!strcmp(key, "usb_disconnect")) {
			gpio_id = GPIO_USB_DISCONNECT;
		} else {
			errx(1, "%s: unknown type \"%s\"", __func__, value);
			exit(1);
		}

		device_parser_accept(dp, YAML_MAPPING_START_EVENT, NULL, 0);

		while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
			device_parser_accept(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH);

			if (!strcmp(key, "chip")) {
				options->gpios[gpio_id].chip = strdup(value);
			} else if (!strcmp(key, "line")) {
				options->gpios[gpio_id].offset = strtoul(value, NULL, 0);
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

static void *local_gpio_open(struct device *dev)
{
	struct local_gpio *local_gpio;

	local_gpio = calloc(1, sizeof(*local_gpio));

	local_gpio->options = dev->control_options;

	if (local_gpio_init(local_gpio) < 0)
		return NULL;

	if (local_gpio->options->gpios[GPIO_POWER_KEY].present)
		dev->has_power_key = true;

	local_gpio_device_power(local_gpio, 0);

	if (dev->usb_always_on)
		local_gpio_device_usb(local_gpio, 1);
	else
		local_gpio_device_usb(local_gpio, 0);

	usleep(500000);

	return local_gpio;
}

static int local_gpio_toggle_io(struct local_gpio *local_gpio, unsigned int gpio, bool on)
{
	if (!local_gpio->options->gpios[gpio].present)
		return -EINVAL;

	if (local_gpio_set_value(local_gpio, gpio, on) < 0)
		warn("%s:%d unable to set value", __func__, __LINE__);

	return 0;
}

static int local_gpio_device_power(struct local_gpio *local_gpio, bool on)
{
	return local_gpio_toggle_io(local_gpio, GPIO_POWER, on);
}

static void local_gpio_device_usb(struct local_gpio *local_gpio, bool on)
{
	local_gpio_toggle_io(local_gpio, GPIO_USB_DISCONNECT, on);
}

static int local_gpio_power(struct device *dev, bool on)
{
	struct local_gpio *local_gpio = dev->cdb;

	return local_gpio_device_power(local_gpio, on);
}

static void local_gpio_usb(struct device *dev, bool on)
{
	struct local_gpio *local_gpio = dev->cdb;

	local_gpio_device_usb(local_gpio, on);
}

static void local_gpio_key(struct device *dev, int key, bool asserted)
{
	struct local_gpio *local_gpio = dev->cdb;

	switch (key) {
	case DEVICE_KEY_FASTBOOT:
		local_gpio_toggle_io(local_gpio, GPIO_FASTBOOT_KEY, asserted);
		break;
	case DEVICE_KEY_POWER:
		local_gpio_toggle_io(local_gpio, GPIO_POWER_KEY, asserted);
		break;
	}
}

const struct control_ops local_gpio_ops = {
	.parse_options = local_gpio_parse_options,
	.open = local_gpio_open,
	.power = local_gpio_power,
	.usb = local_gpio_usb,
	.key = local_gpio_key,
};
