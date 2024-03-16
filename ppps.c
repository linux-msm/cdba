/*
 * Copyright (c) 2023, Linaro Ltd.
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define _GNU_SOURCE /* for asprintf */
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
#include <stdbool.h>

#include "device.h"

#define PPPS_BASE_PATH "/sys/bus/usb/devices/%s/disable"

void ppps_power_path(const char *ppps_path, bool on)
{
	int rc, fd;

	//fprintf(stderr, "ppps_power: %-3s %s\n", on ? "on" : "off", path);

	fd = open(ppps_path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", ppps_path, strerror(errno));
		if (errno != ENOENT)
			fprintf(stderr, "Maybe missing permissions (see https://git.io/JIB2Z)\n");
		return;
	}

	rc = write(fd, on ? "0" : "1", 1);
	if (rc < 0)
		fprintf(stderr, "failed to write to %s: %s\n", ppps_path, strerror(errno));

	close(fd);
}

void ppps_power(struct device *dev, bool on)
{
	/* ppps_path should be like "2-2:1.0/2-2-port2" */
	if (dev->ppps_path[0] != '/') {
		char *temp;

		asprintf(&temp, PPPS_BASE_PATH, dev->ppps_path);
		free(dev->ppps_path);
		dev->ppps_path = temp;
	}

	if (dev->ppps3_path && dev->ppps3_path[0] != '/') {
		char *temp;

		asprintf(&temp, PPPS_BASE_PATH, dev->ppps3_path);
		free(dev->ppps3_path);
		dev->ppps3_path = temp;
	}

	ppps_power_path(dev->ppps_path, on);
	if (dev->ppps3_path)
		ppps_power_path(dev->ppps3_path, on);
}
