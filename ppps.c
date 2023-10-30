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

	if (dev->ppps3_path[0] != '/') {
		char *temp;

		asprintf(&temp, PPPS_BASE_PATH, dev->ppps3_path);
		free(dev->ppps3_path);
		dev->ppps3_path = temp;
	}

	ppps_power_path(dev->ppps_path, on);
	if (dev->ppps3_path)
		ppps_power_path(dev->ppps3_path, on);
}
