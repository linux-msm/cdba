/*
 * Copyright (c) 2024, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cdba-server.h"
#include "device.h"
#include "device_parser.h"
#include "watch.h"

void cdba_send_buf(int type, size_t len, const void *buf)
{
	/* ignore console messages */
}

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s <board> on|off\n", name);
	exit(EXIT_FAILURE);
}

static struct device *selected_device;

bool ready(void)
{
	return device_is_running(selected_device);
}

int main(int argc, char **argv)
{
	const char *home;
	const char *name;
	bool on;
	int ret;

	if (argc != 3)
		usage(argv[0]);

	if (!strcmp(argv[2], "on"))
		on = true;
	else if (!strcmp(argv[2], "off"))
		on = false;
	else
		usage(argv[0]);

	home = getenv("HOME");
	if (home)
		chdir(home);

	ret = device_parser(".cdba");
	if (ret) {
		ret = device_parser("/etc/cdba");
		if (ret) {
			fprintf(stderr, "device parser: unable to open config file\n");
			exit(1);
		}
	}

	name = argv[1];
	selected_device = device_open(name, "nobody");
	if (!selected_device) {
		fprintf(stderr, "failed to open %s\n", name);
		exit(EXIT_FAILURE);
	}

	if (on) {
		device_power(selected_device, true);
		watch_main_loop(ready);

		selected_device->usb_always_on = true;
		selected_device->power_always_on = true;
	} else {
		device_usb(selected_device, false);
		device_power(selected_device, false);
	}

	device_close(selected_device);

	return 0;
}
