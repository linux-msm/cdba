/*
 * Copyright (c) 2018, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdbool.h>
#include <yaml.h>

#include "device.h"
#include "device_parser.h"

#define TOKEN_LENGTH	16384

struct device_parser {
	yaml_parser_t parser;
	yaml_event_t event;
};

static void nextsym(struct device_parser *dp)
{
	if (!yaml_parser_parse(&dp->parser, &dp->event)) {
		fprintf(stderr, "device parser: error %u\n", dp->parser.error);
		exit(1);
	}
}

int device_parser_accept(struct device_parser *dp, int type,
			 char *scalar,  size_t scalar_len)
{
	if (dp->event.type == type) {
		if (scalar && scalar_len > 0) {
			strncpy(scalar, (char *)dp->event.data.scalar.value, scalar_len - 1);
			scalar[scalar_len - 1] = '\0';
		}

		yaml_event_delete(&dp->event);
		nextsym(dp);
		return true;
	} else {
		return false;
	}
}

bool device_parser_expect(struct device_parser *dp, int type,
			  char *scalar,  size_t scalar_len)
{
	if (device_parser_accept(dp, type, scalar, scalar_len)) {
		return true;
	}

	fprintf(stderr, "device parser: expected %d got %u\n", type, dp->event.type);
	exit(1);
}

static void set_control_ops(struct device *dev, const struct control_ops *ops)
{
	if (dev->control_ops) {
		fprintf(stderr, "device parser: control operations are already selected for %s\n", dev->name);
		exit(1);
	}

	dev->control_ops = ops;
}

static void set_console_ops(struct device *dev, const struct console_ops *ops)
{
	if (dev->console_ops) {
		fprintf(stderr, "device parser: console operations are already selected for %s\n", dev->name);
		exit(1);
	}

	dev->console_ops = ops;
}

static void parse_board(struct device_parser *dp)
{
	struct device *dev;
	char value[TOKEN_LENGTH];
	char key[TOKEN_LENGTH];

	dev = calloc(1, sizeof(*dev));

	while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		if (!strcmp(key, "users")) {
			dev->users = calloc(1, sizeof(*dev->users));
			list_init(dev->users);

			if (device_parser_accept(dp, YAML_SCALAR_EVENT, value, 0))
				continue;

			device_parser_expect(dp, YAML_SEQUENCE_START_EVENT, NULL, 0);

			while (device_parser_accept(dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
				struct device_user *user = calloc(1, sizeof(*user));

				user->username = strdup(key);

				list_add(dev->users, &user->node);
			}

			device_parser_expect(dp, YAML_SEQUENCE_END_EVENT, NULL, 0);

			continue;
		}

		if (!strcmp(key, "local_gpio")) {
			dev->control_options = local_gpio_ops.parse_options(dp);
			if (dev->control_options)
				set_control_ops(dev, &local_gpio_ops);
			continue;
		} else if (!strcmp(key, "ftdi_gpio")) {
			dev->control_options = ftdi_gpio_ops.parse_options(dp);
			if (dev->control_options)
				set_control_ops(dev, &ftdi_gpio_ops);
			continue;
		} else if (!strcmp(key, "laurent")) {
			dev->control_options = laurent_ops.parse_options(dp);
			if (dev->control_options)
				set_control_ops(dev, &laurent_ops);
			continue;
		}

		device_parser_expect(dp, YAML_SCALAR_EVENT, value, TOKEN_LENGTH);

		if (!strcmp(key, "board")) {
			dev->board = strdup(value);
		} else if (!strcmp(key, "name")) {
			dev->name = strdup(value);
		} else if (!strcmp(key, "cdba")) {
			dev->control_dev = strdup(value);
			set_control_ops(dev, &cdb_assist_ops);
		} else if (!strcmp(key, "conmux")) {
			/* conmux handles both control and console */
			dev->control_dev = strdup(value);
			dev->console_dev = strdup(value);
			set_control_ops(dev, &conmux_ops);
			set_console_ops(dev, &conmux_console_ops);
		} else if (!strcmp(key, "alpaca")) {
			dev->control_dev = strdup(value);
			set_control_ops(dev, &alpaca_ops);
		} else if (!strcmp(key, "external")) {
			dev->control_dev = strdup(value);
			set_control_ops(dev, &external_ops);
		} else if (!strcmp(key, "qcomlt_debug_board")) {
			dev->control_dev = strdup(value);
			set_control_ops(dev, &qcomlt_dbg_ops);
		} else if (!strcmp(key, "console")) {
			dev->console_dev = strdup(value);
			set_console_ops(dev, &console_ops);
		} else if (!strcmp(key, "voltage")) {
			dev->voltage = strtoul(value, NULL, 10);
		} else if (!strcmp(key, "fastboot")) {
			dev->serial = strdup(value);

			if (!dev->boot)
				dev->boot = device_fastboot_boot;
		} else if (!strcmp(key, "fastboot_set_active")) {
			if (!strcmp(value, "true"))
				dev->set_active = "a";
			else
				dev->set_active = strdup(value);
		} else if (!strcmp(key, "broken_fastboot_boot")) {
			if (!strcmp(value, "true"))
				dev->boot = device_fastboot_flash_reboot;
		} else if (!strcmp(key, "description")) {
			dev->description = strdup(value);
		} else if (!strcmp(key, "fastboot_key_timeout")) {
			dev->fastboot_key_timeout = strtoul(value, NULL, 10);
		} else if (!strcmp(key, "usb_always_on")) {
			dev->usb_always_on = !strcmp(value, "true");
		} else if (!strcmp(key, "ppps_path")) {
			dev->ppps_path = strdup(value);
		} else if (!strcmp(key, "ppps3_path")) {
			dev->ppps3_path = strdup(value);
		} else if (!strcmp(key, "status-cmd")) {
			dev->status_cmd = strdup(value);
		} else if (!strcmp(key, "power_always_on")) {
			dev->power_always_on = !strcmp(value, "true");
		} else if (!strcmp(key, "video_device")) {
			dev->video_device = strdup(value);
		} else {
			fprintf(stderr, "device parser: unknown key \"%s\"\n", key);
			exit(1);
		}
	}

	if (!dev->board || !dev->serial || !dev->console_dev) {
		fprintf(stderr, "device parser: insufficiently defined device\n");
		exit(1);
	}

	device_add(dev);
}

int device_parser(const char *path)
{
	struct device_parser dp;
	char key[TOKEN_LENGTH];
	FILE *fh;

	fh = fopen(path, "r");
	if (!fh)
		return -1;

	if(!yaml_parser_initialize(&dp.parser)) {
		fprintf(stderr, "device parser: failed to initialize parser\n");
		return -1;
	}

	yaml_parser_set_input_file(&dp.parser, fh);

	nextsym(&dp);

	device_parser_expect(&dp, YAML_STREAM_START_EVENT, NULL, 0);

	device_parser_expect(&dp, YAML_DOCUMENT_START_EVENT, NULL, 0);
	device_parser_expect(&dp, YAML_MAPPING_START_EVENT, NULL, 0);

	if (device_parser_accept(&dp, YAML_SCALAR_EVENT, key, TOKEN_LENGTH)) {
		device_parser_expect(&dp, YAML_SEQUENCE_START_EVENT, NULL, 0);

		while (device_parser_accept(&dp, YAML_MAPPING_START_EVENT, NULL, 0)) {
			parse_board(&dp);
			device_parser_expect(&dp, YAML_MAPPING_END_EVENT, NULL, 0);
		}

		device_parser_expect(&dp, YAML_SEQUENCE_END_EVENT, NULL, 0);
	}

	device_parser_expect(&dp, YAML_MAPPING_END_EVENT, NULL, 0);
	device_parser_expect(&dp, YAML_DOCUMENT_END_EVENT, NULL, 0);
	device_parser_expect(&dp, YAML_STREAM_END_EVENT, NULL, 0);

	yaml_event_delete(&dp.event);
	yaml_parser_delete(&dp.parser);

	fclose(fh);

	return 0;
}
